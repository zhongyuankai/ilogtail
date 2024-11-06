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

    LoggroupEntry(std::string & configName,
                std::string & topic,
                LogstoreFeedBackKey & logstoreKey,
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
            ++this->mRead;
            if (--this->mSize == this->LOW_SIZE && !this->mValid) {
                this->mValid = true;
            }
            if (item == NULL) {
                continue;
            }
            logGroupVec.push_back(item);
        }
    }

    // with empty item
    bool InsertItem(LoggroupEntry* item) {
        // mSenderInfo.SetRegion(item->mRegion);
        // if (QueueType::ExactlyOnce == this->mType) {
        //     return insertExactlyOnceItem(item);
        // }

        if (this->IsFull()) {
            return false;
        }
        auto index = this->mRead;
        for (; index < this->mWrite; ++index) {
            if (this->mArray[index % this->SIZE] == NULL) {
                break;
            }
        }
#ifdef LOGTAIL_DEBUG_FLAG
        APSARA_LOG_DEBUG(sLogger,
                         ("Insert send item",
                          item)("size", this->mSize)("read", this->mRead)("write", this->mWrite)("index", index));
#endif
        this->mArray[index % this->SIZE] = item;
        if (index == this->mWrite) {
            ++this->mWrite;
        }
        ++this->mSize;
        if (this->mSize == this->HIGH_SIZE) {
            this->mValid = false;
        }
        return true;
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
        PTScopedLock dataLock(mLock);
        auto& singleQueue = mLogstoreSenderQueueMap[key];

        // For correctness, exactly once queue should ignore mUrgentFlag.
        // if (singleQueue.GetQueueType() == QueueType::ExactlyOnce) {
        //     return singleQueue.IsValid();
        // }

        if (mUrgentFlag) {
            return true;
        }
        return singleQueue.IsValid();
    }

    bool Wait(int32_t waitMs) { return mTrigger.Wait(waitMs); }

    bool IsValid(const LogstoreFeedBackKey& key) {
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
        Signal();
        return true;
    }

    void CheckAndPopAllItem(std::vector<LoggroupEntry*>& itemVec,
                            int32_t curTime,
                            bool& singleQueueFullFlag,
                            std::unordered_map<std::string, int>& regionConcurrencyLimits) {
        singleQueueFullFlag = false;
        PTScopedLock dataLock(mLock);
        if (mLogstoreSenderQueueMap.empty()) {
            return;
        }

        // must check index before moving iterator
        mSenderQueueBeginIndex = mSenderQueueBeginIndex % mLogstoreSenderQueueMap.size();

        // here we set sender queue begin index, let the sender order be different each time
        LogstoreFeedBackQueueMapIterator beginIter = mLogstoreSenderQueueMap.begin();
        std::advance(beginIter, mSenderQueueBeginIndex++);

        PopItem(
            beginIter, mLogstoreSenderQueueMap.end(), itemVec, curTime, regionConcurrencyLimits, singleQueueFullFlag);
        PopItem(
            mLogstoreSenderQueueMap.begin(), beginIter, itemVec, curTime, regionConcurrencyLimits, singleQueueFullFlag);
    }

    static void PopItem(LogstoreFeedBackQueueMapIterator beginIter,
                        LogstoreFeedBackQueueMapIterator endIter,
                        std::vector<LoggroupEntry*>& itemVec,
                        int32_t curTime,
                        std::unordered_map<std::string, int>& regionConcurrencyLimits,
                        bool& singleQueueFullFlag) {
        for (LogstoreFeedBackQueueMapIterator iter = beginIter; iter != endIter; ++iter) {
            SingleLogStoreManager& singleQueue = iter->second;
            if (!singleQueue.IsValidToSend(curTime)) {
                continue;
            }
            singleQueue.GetAllIdleLoggroupWithLimit(itemVec, curTime, regionConcurrencyLimits);
            singleQueueFullFlag |= !singleQueue.IsValid();
        }
    }

    void PopAllItem(std::vector<LoggroupEntry*>& itemVec, int32_t curTime, bool& singleQueueFullFlag) {
        singleQueueFullFlag = false;
        PTScopedLock dataLock(mLock);
        for (LogstoreFeedBackQueueMapIterator iter = mLogstoreSenderQueueMap.begin();
             iter != mLogstoreSenderQueueMap.end();
             ++iter) {
            iter->second.GetAllLoggroup(itemVec);
        }
    }

    bool IsEmpty() {
        PTScopedLock dataLock(mLock);
        for (LogstoreFeedBackQueueMapIterator iter = mLogstoreSenderQueueMap.begin();
             iter != mLogstoreSenderQueueMap.end();
             ++iter) {
            if (!iter->second.IsEmpty()) {
                return false;
            }
        }
        return true;
    }

    bool IsEmpty(const LogstoreFeedBackKey& key) {
        PTScopedLock dataLock(mLock);
        auto iter = mLogstoreSenderQueueMap.find(key);
        return iter == mLogstoreSenderQueueMap.end() || iter->second.IsEmpty();
    }

    void Delete(const LogstoreFeedBackKey& key) {
        PTScopedLock dataLock(mLock);
        auto iter = mLogstoreSenderQueueMap.find(key);
        if (iter != mLogstoreSenderQueueMap.end()) {
            mLogstoreSenderQueueMap.erase(iter);
        }
    }

    // do not clear real data, just for unit test
    void RemoveAll() {
        PTScopedLock dataLock(mLock);
        mLogstoreSenderQueueMap.clear();
        mSenderQueueBeginIndex = 0;
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
        PTScopedLock dataLock(mLock);
        size_t queueSize = 0;
        for (LogstoreFeedBackQueueMapIterator iter = mLogstoreSenderQueueMap.begin();
             iter != mLogstoreSenderQueueMap.end();
             ++iter) {
            queueSize += iter->second.GetSize();
        }
        return queueSize;
    }

protected:
    LogstoreFeedBackQueueMap mLogstoreSenderQueueMap;
    PTMutex mLock;
    TriggerEvent mTrigger;
    LogstoreFeedBackInterface* mFeedBackObj;
    bool mUrgentFlag;
    size_t mSenderQueueBeginIndex;
};

} // namespace logtail
