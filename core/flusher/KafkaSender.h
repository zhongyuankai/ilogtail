#pragma once

#include "common/Lock.h"
#include "common/Thread.h"
#include "flusher/KafkaSenderQueueParam.h"
#include "flusher/FlusherKafka.h"
#include "flusher/KafkaSenderQueue.h"
#include "librdkafka/rdkafkacpp.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>
#include <atomic>


namespace logtail {

class KafkaSenderCallback : public RdKafka::DeliveryReportCb {
public:
    void dr_cb(RdKafka::Message & message);

    size_t getSendFailedCount() {
        return mSendFailedCount.load();
    }

    std::atomic_int mSendFailedCount;
};

using KafkaSenderCallbackPtr = std::shared_ptr<KafkaSenderCallback>;
using KafkaProducerPtr = std::shared_ptr<RdKafka::Producer>;

struct ThreadFlags {
    volatile bool mFlushLog = false;
    volatile bool mInteruptFlag = false;
};
using ThreadFlagsPtr = std::shared_ptr<ThreadFlags>;

struct KafkaProducerEntry {
    KafkaProducerPtr mProducer;
    KafkaSenderCallbackPtr mCallback;
    ThreadFlagsPtr mThreadFlags;
    ThreadPtr mThread;
    int32_t mThreadNo;
    int32_t mTaskCount;

    KafkaProducerEntry(KafkaProducerPtr producer, KafkaSenderCallbackPtr callback)
        : mProducer(producer)
        , mCallback(callback)
        , mThreadFlags(std::make_shared<ThreadFlags>())
        , mTaskCount(1) {
    }
    
    void setThread(ThreadPtr thread, int32_t threadNo) {
        mThread = thread;
        mThreadNo = threadNo;
    }
};

class KafkaSender {
public:
    static KafkaSender * Instance();

    bool PushQueue(LoggroupEntry * data);

    bool CreateKafkaProducer(FlusherKafka * flusherKafka);
    void RemoveKafkaProducer(int64_t kafkaProducerKey);

    void ClearKafakProducerThread();

    ~KafkaSender();

    bool Send(std::vector<PipelineEventGroup> & eventGroupList, size_t logSize, const FlusherKafka * flusherKafka);

    LogstoreFeedBackInterface* GetSenderFeedBackInterface();
    void SetFeedBackInterface(LogstoreFeedBackInterface* pProcessInterface);

    void SetQueueUrgent();

    bool FlushOut(int32_t time_interval_in_mili_seconds);

    void OnSendDone(LoggroupEntry * data, KafkaSendInfo::SendResult sendRst);

    LoggroupOffsetMap GetSendFailedOffsets();

    bool RegisterKafkaFlusher(FlusherKafka * flusherKafka);
    void RemoveKafkaFlusher(FlusherKafka * flusherKafka, bool removeQueue);

    KafkaSendStatistics GetSenderStatistics(const LogstoreFeedBackKey & key);

private:
    KafkaSender();

    void DaemonSender(int64_t kafkaProducerKey);

    bool IsFlush();
    void SetFlush();
    void ResetFlush();

    std::atomic_int32_t mSendBufferCount{0};
    std::atomic_size_t mSendBufferBytes{0};
    std::atomic_size_t mSendLines{0};
    std::atomic_size_t mSendErrorCount{0};
    std::atomic_size_t mSendErrorQueueFullCount{0};
    std::atomic_uint64_t mSendTime{0};
    std::atomic_int mPushQueueFailedCount{0};

    KafkaSenderQueue mSenderQueue;

    std::mutex mKafkaProducersMutex;
    std::unordered_map<int64_t, std::shared_ptr<KafkaProducerEntry>> mKafkaProducers;
    std::unordered_map<std::string, int32_t> mKafkaClusterCounter;
    std::unordered_map<LogstoreFeedBackKey, int32_t> mLogstoreKafkaProducerMap;

    int32_t mThreadNo = 0;
    std::atomic_int mMetricThreadNo{0};
    bool mNeedClearThead = false;
};

} // namespace logtail
