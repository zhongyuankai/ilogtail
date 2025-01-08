#pragma once

#include "FileInfo.h"
#include "LogstoreFeedbackKey.h"
#include "logger/Logger.h"
#include "common/LogstoreFeedbackQueue.h"
#include "common/LogGroupContext.h"
#include "common/Lock.h"
#include "flusher/KafkaSenderQueueParam.h"
#include "flusher/LoggroupOffsetMap.h"
#include "sonic/sonic.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>
#include <deque>
#include <unordered_set>
#include <utility>
#include <vector>
#include <stdio.h>
#include <sstream>

namespace logtail {

using WriteBuffer = sonic_json::WriteBuffer;
using WriteBufferPtr = std::unique_ptr<WriteBuffer>;

enum LoggroupKafkaSendStatus { LoggroupKafkaSendStatus_Idle, LoggroupKafkaSendStatus_Sending, LoggroupKafkaSendStatus_Ok };

struct LoggroupEntry {
    std::string mTopic;
    LogstoreFeedBackKey mLogstoreKey;
    int64_t mKafkaProducerKey;
    WriteBufferPtr mLogData;
    int32_t mLogLines;
    time_t mCreateTime;

    size_t mIndex;

    /// <fileKey, <fileHeadMd5, offset>>
    LoggroupOffsetMap mFileOffsets;
    size_t mSendFailedCount;

    LoggroupKafkaSendStatus mStatus;

    LoggroupEntry(const std::string & topic,
                 const LogstoreFeedBackKey & logstoreKey,
                 int64_t kafkaProducerKey)
        : mTopic(topic)
        , mLogstoreKey(logstoreKey)
        , mKafkaProducerKey(kafkaProducerKey)
        , mLogData(std::make_unique<WriteBuffer>())
        , mLogLines(0)
        , mCreateTime(time(nullptr))
        , mIndex(0)
        , mSendFailedCount(0)
        , mStatus(LoggroupKafkaSendStatus_Idle) {
    }

    std::string ConvertFileOffsetToString() {
        std::ostringstream oss;
        for (auto it = mFileOffsets.begin(); it != mFileOffsets.end(); ++it) {
            if (it != mFileOffsets.begin()) {
                oss << " | ";
            }
            oss << it->first.mFileKey << "_" << it->first.mFileHeadMd5 << ":" << it->second;
        }
        return oss.str();
    }
};

struct KafkaSendStatistics{
    KafkaSendStatistics()
    : mMaxUnsendTime(0)
    , mMinUnsendTime(0)
    , mMaxSendSuccessTime(0)
    , mSendQueueSize(0)
    , mSendSuccessCount(0)
    , mSendErrorCount(0)
    , mSendBlockFlag(false)
    , mValidToSendFlag(false) {
    }

    void Reset();

    int32_t mMaxUnsendTime;
    int32_t mMinUnsendTime;
    int32_t mMaxSendSuccessTime;
    uint32_t mSendQueueSize;

    uint32_t mSendSuccessCount;
    uint32_t mSendErrorCount;
    bool mSendBlockFlag;
    bool mValidToSendFlag;
};

struct KafkaSendInfo{
    enum SendResult {
        SendResult_OK,
        SendResult_Sending,
        SendResult_SendFail,
        SendResult_OtherFail
    };

    volatile int32_t mLastSendErrorCount;
    volatile int32_t mLastSendErrorTime;
    volatile int32_t mSendConcurrency;
    volatile bool mSendValidFlag;

    KafkaSendInfo();

    bool CanSend(int32_t curTime);

    void RecordSendResult(SendResult rst, KafkaSendStatistics & statisticsItem);
};


struct KafkaProducerQueue;
using KafkaProducerQueuePtr = std::shared_ptr<KafkaProducerQueue>;

template <class PARAM>
class KafkaSendQueue : public SingleLogstoreFeedbackQueue<LoggroupEntry *, PARAM> {
public:
    explicit KafkaSendQueue(KafkaProducerQueuePtr & kafkaProducerQueue)
        : mKafkaProducerQueue(kafkaProducerQueue) {
    }

    bool IsValid() const override; 

    void GetAllLoggroupWithLimit(std::vector<LoggroupEntry *> & logGroupVec);
    void GetAllLoggroup(std::vector<LoggroupEntry *> & logGroupVec);

    // with empty item
    bool InsertItem(LoggroupEntry * item);

    int32_t RemoveItem(LoggroupEntry * item);

    void OnSendDone(LoggroupEntry * item, KafkaSendInfo::SendResult sendRst);

    bool IsValidToSend(int32_t curTime);

    KafkaSendStatistics GetSenderStatistics();

    KafkaProducerQueuePtr mKafkaProducerQueue;

    KafkaSendInfo mSendInfo;
    KafkaSendStatistics mSendStatistics;
};

using KafkaSendQueuePtr = std::shared_ptr<KafkaSendQueue<KafkaSenderQueueParam>>;
using KafkaSendQueueMap = std::unordered_map<LogstoreFeedBackKey, KafkaSendQueuePtr>;
using KafkaSendQueueMapIterator = KafkaSendQueueMap::iterator;

struct KafkaProducerQueue {
    int64_t mKafkaProducerKey;
    size_t mSenderQueueBeginIndex;
    KafkaSendQueueMap mSendQueueMap;
    TriggerEvent mTrigger;

    explicit KafkaProducerQueue(int64_t kafkaProducerKey)
        : mKafkaProducerKey(kafkaProducerKey)
        , mSenderQueueBeginIndex(0) {
    }

    void CheckAndPopAllItem(std::vector<LoggroupEntry *> & itemVec, int32_t curTime);
    void PopAllItem(std::vector<LoggroupEntry *> & itemVec, int32_t curTime);

    void PopItem(KafkaSendQueueMapIterator beginIter,
                 KafkaSendQueueMapIterator endIter,
                 std::vector<LoggroupEntry*> & itemVec,
                 int32_t curTime);
};

using KafkaProducerQueueMap = std::unordered_map<int64_t, KafkaProducerQueuePtr>;

class KafkaSenderQueue : public LogstoreFeedBackInterface {
public:
    KafkaSenderQueue() : mQueueSize(0), mMaxQueueSize(0), mFeedBackObj(nullptr), mUrgentFlag(false) {}

    void SetParam(const size_t lowSize, const size_t highSize, const size_t maxSize);

    void SetMaxQueueSize(const size_t maxQueueSize);

    void SetFeedBackObject(LogstoreFeedBackInterface * pFeedbackObj);

    void Signal();

    void FeedBack(const LogstoreFeedBackKey & key) override;

    bool IsValidToPush(const LogstoreFeedBackKey & key, FeedBackStatusPtr status = nullptr) override;

    bool Wait(int64_t kafkaProducerKey, int32_t waitMs);

    bool PushItem(const LogstoreFeedBackKey & key, LoggroupEntry * const & item);

    void CheckAndPopAllItemByKafkaProducerKey(std::vector<LoggroupEntry *> & itemVec, int64_t kafkaProducerKey, int32_t curTime);

    void PopAllItemByKafkaProducerKey(std::vector<LoggroupEntry *> & itemVec, int64_t kafkaProducerKey, int32_t curTime);

    bool IsEmpty();

    bool IsEmpty(const LogstoreFeedBackKey & key);

    void SetUrgent();

    void ResetUrgent();

    KafkaSendStatistics GetSenderStatistics(const LogstoreFeedBackKey & key);

    void GetStatus(int32_t & invalidCount, int32_t & totalCount, int32_t & queueSize);

    size_t GetSize();

    size_t GetSize(int64_t kafkaProducerKey);

    void OnLoggroupSendDone(LoggroupEntry * item, KafkaSendInfo::SendResult sendRst);

    LoggroupOffsetMap GetSendFailedOffsets();

    void CreateKafkaSendQueue(const LogstoreFeedBackKey & key, int64_t kafkaProducerKey);

private:
    KafkaProducerQueuePtr getKafkaProducerQueue(int64_t kafkaProducerKey); 
    KafkaSendQueuePtr getKafkaSendQueue(const LogstoreFeedBackKey & key);

    void addSendFailedOffset(LoggroupOffsetMap & fileOffsets);

    void removeItem(KafkaSendQueuePtr & queue, LoggroupEntry * item);

    PTMutex mLock;
    KafkaSendQueueMap mKafkaSendQueue;
    KafkaProducerQueueMap mKafkaProducerQueue;
    size_t mQueueSize;
    size_t mMaxQueueSize;

    std::unordered_set<LoggroupEntry *> mSendingEntrys;

    LoggroupOffsetMap mSendFailedOffsets;

    LogstoreFeedBackInterface * mFeedBackObj;
    bool mUrgentFlag;
};

} // namespace logtail
