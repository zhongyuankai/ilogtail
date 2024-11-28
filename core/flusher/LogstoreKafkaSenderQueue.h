#pragma once

#include "logger/Logger.h"
#include "common/LogstoreFeedbackQueue.h"
#include "common/LogGroupContext.h"
#include "common/Lock.h"
#include "sonic/sonic.h"

#include <unordered_map>
#include <string>
#include <deque>
#include <stdio.h>

namespace logtail {

using WriteBuffer = sonic_json::WriteBuffer;

struct LoggroupEntry {
    std::string mConfigName;
    std::string mTopic;
    LogstoreFeedBackKey mLogstoreKey;
    int64_t mKafkaProducerKey;
    WriteBuffer mLogData;
    int32_t mLogLines;
    time_t mCreateTime;

    size_t index;

    LoggroupEntry(const std::string & configName,
                const std::string & topic,
                LogstoreFeedBackKey logstoreKey,
                int64_t kafkaProducerKey,
                WriteBuffer logData,
                int32_t logLines)
        : mConfigName(configName)
        , mTopic(topic)
        , mLogstoreKey(logstoreKey)
        , mKafkaProducerKey(kafkaProducerKey)
        , mLogData(std::move(logData))
        , mLogLines(logLines)
        , mCreateTime(time(nullptr)) {
    }
};

template <class PARAM>
class SingleLogstoreKafkaSenderManager : public SingleLogstoreFeedbackQueue<LoggroupEntry*, PARAM> {
public:
    SingleLogstoreKafkaSenderManager() {
    }

    void GetAllLoggroup(std::vector<LoggroupEntry*>& logGroupVec) {
        if (this->mSize == 0) {
            return;
        }

        uint64_t index = this->mRead;
        const uint64_t endIndex = this->mWrite;
        for (; index < endIndex; ++index) {
            LoggroupEntry* item = this->mArray[index % this->SIZE];
            if (item == NULL) {
                continue;
            }
            logGroupVec.push_back(item);
        }
    }

    // with empty item
    bool InsertItem(LoggroupEntry* item) {
        if (this->IsFull()) {
            return false;
        }
        auto index = this->mRead;
        for (; index < this->mWrite; ++index) {
            if (this->mArray[index % this->SIZE] == NULL) {
                break;
            }
        }
        this->mArray[index % this->SIZE] = item;
        item->index = index;
        if (index == this->mWrite) {
            ++this->mWrite;
        }
        ++this->mSize;
        if (this->mSize == this->HIGH_SIZE) {
            this->mValid = false;
        }
        return true;
    }

    int32_t RemoveItem(LoggroupEntry * item) {
        if (this->IsEmpty()) {
            return 0;
        }

        auto index = item->index;
        auto dataItem = this->mArray[index % this->SIZE];
        if (dataItem == item) {
            delete dataItem;
            this->mArray[index % this->SIZE] = NULL;
        }
        if (index == this->mWrite) {
            LOG_ERROR(
                sLogger,
                ("find no sender item, configName", item->mConfigName)(
                    "lines", item->mLogLines));
            return 0;
        }
        // need to check mWrite to avoid dead while
        while (this->mRead < this->mWrite && this->mArray[this->mRead % this->SIZE] == NULL) {
            ++this->mRead;
        }
        int rst = 1;
        if (--this->mSize == this->LOW_SIZE && !this->mValid) {
            this->mValid = true;
            rst = 2;
        }
        return rst;
    }
};

template <class PARAM>
class LogstoreKafkaSenderQueue : public LogstoreFeedBackInterface {
protected:
    typedef SingleLogstoreKafkaSenderManager<PARAM> SingleLogStoreManager;
    typedef std::unordered_map<LogstoreFeedBackKey, SingleLogStoreManager> LogstoreFeedBackQueueMap;
    typedef typename std::unordered_map<LogstoreFeedBackKey, SingleLogStoreManager>::iterator
        LogstoreFeedBackQueueMapIterator;

    struct SingleKafkaSenderQueue {
        LogstoreFeedBackQueueMap mLogstoreSenderQueueMap;
        PTMutex mLock;
        TriggerEvent mTrigger;
    };

    using SingleKafkaSenderQueuePtr = std::shared_ptr<SingleKafkaSenderQueue>;
    using MuliLevelKafkaSenderQueue = std::unordered_map<int64_t, SingleKafkaSenderQueuePtr>;
    using LogstoreFeedBackKeyMap = std::unordered_map<LogstoreFeedBackKey, int64_t>;

public:
    LogstoreKafkaSenderQueue() : mFeedBackObj(NULL), mUrgentFlag(false) {}

    void SetParam(const size_t lowSize, const size_t highSize, const size_t maxSize) {
        PARAM* pParam = PARAM::GetInstance();
        pParam->SetLowSize(lowSize);
        pParam->SetHighSize(highSize);
        pParam->SetMaxSize(maxSize);
    }

    void SetMaxQueueSize(const size_t maxQueueSize) {
        mMaxQueueSize = maxQueueSize;
    }

    void SetFeedBackObject(LogstoreFeedBackInterface* pFeedbackObj) {
        PTScopedLock dataLock(mLock);
        mFeedBackObj = pFeedbackObj;
    }

    void Signal() {
        std::vector<SingleKafkaSenderQueuePtr> queues;
        {
            PTScopedLock dataLock(mLock);
            for (const auto & [k, v] : mSenderQueue) {
                queues.push_back(v);
            }
        }
        for (const auto & queue : queues) {
            queue->mTrigger.Trigger();
        }
    }

    virtual void FeedBack(const LogstoreFeedBackKey& key) {
        SingleKafkaSenderQueuePtr queue = getKafkaSenderQueue(key);
        if (!queue) {
            return;
        }
        queue->mTrigger.Trigger();
    }

    virtual bool IsValidToPush(const LogstoreFeedBackKey& key) {
        SingleKafkaSenderQueuePtr queue = nullptr;
        {
            PTScopedLock dataLock(mLock);
            if (mQueueSize.load() > mMaxQueueSize) {
                return false;
            }

            if (mUrgentFlag) {
                return true;
            }
            queue = getKafkaSenderQueueUnLock(key);
        }
        if (!queue) {
            return true;
        }

        PTScopedLock lock(queue->mLock);
        auto& singleQueue = queue->mLogstoreSenderQueueMap[key];
        return singleQueue.IsValid();
    }

    bool Wait(int64_t kafkaProducerKey, int32_t waitMs) {
        auto queue = getOrCreateKafkaSenderQueue(kafkaProducerKey);
        return queue->mTrigger.Wait(waitMs);
    }

    bool PushItem(const LogstoreFeedBackKey& key, LoggroupEntry* const& item) {
        auto queue = getOrCreateKafkaSenderQueue(item->mKafkaProducerKey);
        
        {
            PTScopedLock lock(queue->mLock);
            SingleLogStoreManager& singleQueue = queue->mLogstoreSenderQueueMap[key];
            if (!singleQueue.InsertItem(item)) {
                return false;
            }
        }
        ++mQueueSize;
        queue->mTrigger.Trigger();
        return true;
    }

    void PopItemByKafkaProducerKey(std::vector<LoggroupEntry*> & itemVec, int64_t kafkaProducerKey) {
        auto queue = getOrCreateKafkaSenderQueue(kafkaProducerKey);

        {
            PTScopedLock lock(queue->mLock);
            for (LogstoreFeedBackQueueMapIterator iter = queue->mLogstoreSenderQueueMap.begin();
                iter != queue->mLogstoreSenderQueueMap.end(); ++iter) {
                iter->second.GetAllLoggroup(itemVec);
            }
        }
    }

    void RemoveItem(LoggroupEntry * item) {
        if (item == NULL) {
            return;
        }

        LogstoreFeedBackKey key = item->mLogstoreKey;
        int64_t kafkaProducerKey = item->mKafkaProducerKey;
        auto queue = getOrCreateKafkaSenderQueue(kafkaProducerKey);

        int rst = 0;
        {
            PTScopedLock lock(queue->mLock);
            SingleLogStoreManager& singleQueue = queue->mLogstoreSenderQueueMap[key];
            rst = singleQueue.RemoveItem(item);
        }
        if (rst == 2 && mFeedBackObj != NULL) {
            APSARA_LOG_DEBUG(sLogger, ("OnLoggroupSendDone feedback", ""));
            mFeedBackObj->FeedBack(key);
        }
        if (rst != 0) {
            --mQueueSize;
        }
    }

    bool IsEmpty() {
        return mQueueSize.load() == 0;
    }

    bool IsEmpty(const LogstoreFeedBackKey& key) {
        SingleKafkaSenderQueuePtr queue = getKafkaSenderQueue(key);
        if (!queue) {
            return true;
        }

        PTScopedLock lock(queue->mLock);
        auto iter = queue->mLogstoreSenderQueueMap.find(key);
        return iter == queue->mLogstoreSenderQueueMap.end() || iter->second.IsEmpty();
    }

    void SetUrgent() { mUrgentFlag = true; }

    void ResetUrgent() { mUrgentFlag = false; }

    void GetStatus(int32_t& invalidCount,
                   int32_t& totalCount,
                   int32_t& queueSize) {
        std::vector<SingleKafkaSenderQueuePtr> queues;
        {
            PTScopedLock dataLock(mLock);
            for (const auto & [k, v] : mSenderQueue) {
                queues.push_back(v);
            }
        }
        for (const auto & queue : queues) {
            PTScopedLock dataLock(queue->mLock);
            for (LogstoreFeedBackQueueMapIterator iter = queue->mLogstoreSenderQueueMap.begin();
                    iter != queue->mLogstoreSenderQueueMap.end();  ++iter) {
                ++totalCount;
                if (!iter->second.IsValid()) {
                    ++invalidCount;
                }
                queueSize += iter->second.GetSize();
            }
        }
    }

    size_t GetSize() {
        return mQueueSize.load();
    }

    size_t GetSize(int64_t kafkaProducerKey) {
        auto queue = getOrCreateKafkaSenderQueue(kafkaProducerKey);

        size_t queueSize = 0;
        PTScopedLock lock(queue->mLock);
        for (LogstoreFeedBackQueueMapIterator iter = queue->mLogstoreSenderQueueMap.begin();
                iter != queue->mLogstoreSenderQueueMap.end();  ++iter) {
            queueSize += iter->second.GetSize();
        }
        return queueSize;
    }

protected:
    SingleKafkaSenderQueuePtr getKafkaSenderQueue(const LogstoreFeedBackKey& key) {
        PTScopedLock dataLock(mLock);
        return getKafkaSenderQueueUnLock(key);
    }

    SingleKafkaSenderQueuePtr getKafkaSenderQueueUnLock(const LogstoreFeedBackKey& key) {
        auto it = mLogstoreFeedBackKeyMap.find(key);
        if (it == mLogstoreFeedBackKeyMap.end()) {
            return nullptr;
        }

        auto queue_iter = mSenderQueue.find(it->second);
        if (queue_iter == mSenderQueue.end()) {
            return nullptr;
        }
        return queue_iter->second;
    }

    SingleKafkaSenderQueuePtr getOrCreateKafkaSenderQueue(int64_t kafkaProducerKey) {
        PTScopedLock dataLock(mLock);
        auto queue_iter = mSenderQueue.find(kafkaProducerKey);
        if (queue_iter == mSenderQueue.end()) {
            auto queue = std::make_shared<SingleKafkaSenderQueue>();
            mSenderQueue.emplace(kafkaProducerKey, queue);
            return queue;
        }
        return queue_iter->second;
    }

    PTMutex mLock;
    MuliLevelKafkaSenderQueue mSenderQueue;
    LogstoreFeedBackKeyMap mLogstoreFeedBackKeyMap;
    LogstoreFeedBackInterface* mFeedBackObj;
    bool mUrgentFlag;

    std::atomic<size_t> mQueueSize = 0;
    size_t mMaxQueueSize;
};

} // namespace logtail
