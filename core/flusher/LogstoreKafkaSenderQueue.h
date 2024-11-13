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

struct LogstoreKafkaSenderStatistics {
    LogstoreKafkaSenderStatistics();
    void Reset();

    int32_t mMaxUnsendTime;
    int32_t mMinUnsendTime;
    int32_t mMaxSendSuccessTime;
    uint32_t mSendQueueSize;
    uint32_t mSendNetWorkErrorCount;
    uint32_t mSendQuotaErrorCount;
    uint32_t mSendDiscardErrorCount;
    uint32_t mSendSuccessCount;
    bool mSendBlockFlag;
    bool mValidToSendFlag;
};

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

struct LogstoreKafkaSenderInfo {
    enum SendResult {
        SendResult_OK,
        SendResult_Buffered, // move to file buffer
        SendResult_NetworkFail,
        SendResult_QuotaFail,
        SendResult_DiscardFail,
        SendResult_OtherFail
    };

    std::string mRegion;
    volatile int32_t mLastNetworkErrorCount;
    volatile int32_t mLastQuotaExceedCount;
    volatile int32_t mLastNetworkErrorTime;
    volatile int32_t mLastQuotaExceedTime;
    volatile double mNetworkRetryInterval;
    volatile double mQuotaRetryInterval;
    volatile bool mNetworkValidFlag;
    volatile bool mQuotaValidFlag;
    volatile int32_t mSendConcurrency;
    volatile int32_t mSendConcurrencyUpdateTime;

    LogstoreKafkaSenderInfo();

    void SetRegion(const std::string& region);

    bool ConcurrencyValid();
    void ConcurrencyDec() { --mSendConcurrency; }

    bool CanSend(int32_t curTime);
    // RecordSendResult
    // @return true if need to trigger.
    bool RecordSendResult(SendResult rst, LogstoreKafkaSenderStatistics& statisticsItem);
    // return value, recover sucess flag(if logstore is invalid before, return true, else return false)
    bool OnRegionRecover(const std::string& region);
};

template <class PARAM>
class SingleLogstoreKafkaSenderManager : public SingleLogstoreFeedbackQueue<LoggroupEntry*, PARAM> {
public:
    SingleLogstoreKafkaSenderManager()
        : mLastSendTimeSecond(0), mLastSecondTotalBytes(0), mMaxSendBytesPerSecond(-1), mFlowControlExpireTime(0) {}

    void SetMaxSendBytesPerSecond(int32_t maxBytes, int32_t expireTime) {
        mMaxSendBytesPerSecond = maxBytes;
        mFlowControlExpireTime = expireTime;
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

    LogstoreKafkaSenderStatistics GetSenderStatistics() {
        LogstoreKafkaSenderStatistics statisticsItem = mSenderStatistics;
        mSenderStatistics.Reset();

        statisticsItem.mSendBlockFlag = !this->IsValid();
        statisticsItem.mValidToSendFlag = IsValidToSend(time(NULL));
        if (this->IsEmpty()) {
            return statisticsItem;
        }

        uint64_t index = QueueType::ExactlyOnce == this->mType ? 0 : this->mRead;
        int32_t minSendTime = INT32_MAX;
        int32_t maxSendTime = 0;
        for (size_t i = 0; i < this->mSize; ++i, ++index) {
            LoggroupEntry* item = this->mArray[index % this->SIZE];
            if (item == NULL) {
                continue;
            }

            // if (item->mEnqueueTime < minSendTime) {
            //     minSendTime = item->mEnqueueTime;
            // }
            // if (item->mEnqueueTime > maxSendTime) {
            //     maxSendTime = item->mEnqueueTime;
            // }
            ++statisticsItem.mSendQueueSize;
        }
        if (statisticsItem.mSendQueueSize > 0) {
            statisticsItem.mMinUnsendTime = minSendTime;
            statisticsItem.mMaxUnsendTime = maxSendTime;
        }
        return statisticsItem;
    }

    bool IsValidToSend(int32_t curTime) {
        return mSenderInfo.CanSend(curTime);
    }

    LogstoreKafkaSenderInfo mSenderInfo;
    LogstoreKafkaSenderStatistics mSenderStatistics;

    // add for flow control
    volatile int32_t mLastSendTimeSecond;
    volatile int32_t mLastSecondTotalBytes;

    volatile int32_t mMaxSendBytesPerSecond; // <0, no flowControl, 0 pause sending,
    volatile int32_t mFlowControlExpireTime; // <=0 no expire

    std::vector<RangeCheckpointPtr> mRangeCheckpoints;
    std::deque<LoggroupEntry*> mExtraBuffers;
};

template <class PARAM>
class LogstoreKafkaSenderQueue : public LogstoreFeedBackInterface {
protected:
    typedef SingleLogstoreKafkaSenderManager<PARAM> SingleLogStoreManager;
    typedef std::unordered_map<LogstoreFeedBackKey, SingleLogStoreManager> LogstoreFeedBackQueueMap;
    typedef typename std::unordered_map<LogstoreFeedBackKey, SingleLogStoreManager>::iterator
        LogstoreFeedBackQueueMapIterator;

public:
    LogstoreKafkaSenderQueue() : mFeedBackObj(NULL), mUrgentFlag(false), mSenderQueueBeginIndex(0) {}

    void SetParam(const size_t lowSize, const size_t highSize, const size_t maxSize) {
        PARAM* pParam = PARAM::GetInstance();
        pParam->SetLowSize(lowSize);
        pParam->SetHighSize(highSize);
        pParam->SetMaxSize(maxSize);
    }

    void SetFeedBackObject(LogstoreFeedBackInterface* pFeedbackObj) {
        PTScopedLock dataLock(mLock);
        mFeedBackObj = pFeedbackObj;
    }

    void Signal() { mTrigger.Trigger(); }

    virtual void FeedBack(const LogstoreFeedBackKey& key) { mTrigger.Trigger(); }

    virtual bool IsValidToPush(const LogstoreFeedBackKey& key) {
        if (mSize.load() > 1000) {
            return false;
        }
        PTScopedLock dataLock(mLock);
        auto& singleQueue = mLogstoreSenderQueueMap[key];

        if (mUrgentFlag) {
            return true;
        }
        return singleQueue.IsValid();
    }

    bool Wait(int32_t waitMs) { return mTrigger.Wait(waitMs); }

    bool IsValid(const LogstoreFeedBackKey& key) {
        if (mSize.load() > 1000) {
            return false;
        }
        PTScopedLock dataLock(mLock);
        SingleLogStoreManager& singleQueue = mLogstoreSenderQueueMap[key];
        return singleQueue.IsValid();
    }

    bool PushItem(const LogstoreFeedBackKey& key, LoggroupEntry* const& item) {
        {
            PTScopedLock dataLock(mLock);
            SingleLogStoreManager& singleQueue = mLogstoreSenderQueueMap[key];
            if (!singleQueue.InsertItem(item)) {
                return false;
            }
        }
        ++mSize;
        Signal();
        return true;
    }

    void PopAllItem(std::vector<LoggroupEntry*> & itemVec) {
        PTScopedLock dataLock(mLock);
        for (LogstoreFeedBackQueueMapIterator iter = mLogstoreSenderQueueMap.begin();
             iter != mLogstoreSenderQueueMap.end();
             ++iter) {
            iter->second.GetAllLoggroup(itemVec);
        }
    }

    void RemoveItem(LoggroupEntry * item) {
        if (item == NULL) {
            return;
        }

        LogstoreFeedBackKey key = item->mLogstoreKey;
        int rst = 0;
        {
            PTScopedLock dataLock(mLock);
            SingleLogStoreManager& singleQueue = mLogstoreSenderQueueMap[key];
            rst = singleQueue.RemoveItem(item);
        }
        if (rst == 2 && mFeedBackObj != NULL) {
            APSARA_LOG_DEBUG(sLogger, ("OnLoggroupSendDone feedback", ""));
            mFeedBackObj->FeedBack(key);
        }
        if (rst > 0) {
            --mSize;
        }
    }

    bool IsEmpty() {
        return mSize.load() == 0;
    }

    bool IsEmpty(const LogstoreFeedBackKey& key) {
        PTScopedLock dataLock(mLock);
        auto iter = mLogstoreSenderQueueMap.find(key);
        return iter == mLogstoreSenderQueueMap.end() || iter->second.IsEmpty();
    }

    void Lock() { mLock.lock(); }

    void Unlock() { mLock.unlock(); }

    void SetUrgent() { mUrgentFlag = true; }

    void ResetUrgent() { mUrgentFlag = false; }

    LogstoreKafkaSenderStatistics GetSenderStatistics(const LogstoreFeedBackKey& key) {
        PTScopedLock dataLock(mLock);
        LogstoreFeedBackQueueMapIterator iter = mLogstoreSenderQueueMap.find(key);
        if (iter == mLogstoreSenderQueueMap.end()) {
            return LogstoreKafkaSenderStatistics();
        }
        return iter->second.GetSenderStatistics();
    }

    void GetStatus(int32_t& invalidCount,
                   int32_t& totalCount,
                   int32_t& queueSize) {
        PTScopedLock dataLock(mLock);
        for (LogstoreFeedBackQueueMapIterator iter = mLogstoreSenderQueueMap.begin();
             iter != mLogstoreSenderQueueMap.end();
             ++iter) {
            ++totalCount;
            if (!iter->second.IsValid()) {
                ++invalidCount;
            }
            queueSize += iter->second.GetSize();
        }
    }

    size_t GetSize() {
        return mSize.load();
    }

protected:
    LogstoreFeedBackQueueMap mLogstoreSenderQueueMap;
    PTMutex mLock;
    TriggerEvent mTrigger;
    LogstoreFeedBackInterface* mFeedBackObj;
    bool mUrgentFlag;
    size_t mSenderQueueBeginIndex;

    std::atomic_int mSize;
};

} // namespace logtail
