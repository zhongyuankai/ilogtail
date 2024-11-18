#pragma once

#include "common/WaitObject.h"
#include "common/Lock.h"
#include "common/Thread.h"
#include "flusher/KafkaSenderQueueParam.h"
#include "flusher/FlusherKafka.h"
#include "flusher/LogstoreKafkaSenderQueue.h"
#include "librdkafka/rdkafkacpp.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <iostream>
#include <atomic>


namespace logtail {

class KafkaSenderCallback : public RdKafka::DeliveryReportCb {
public:
    KafkaSenderCallback(std::atomic_int & sendFailedCount_)
        : sendFailedCount(sendFailedCount_) {
    }

    void dr_cb(RdKafka::Message & message);

    std::atomic_int & sendFailedCount;
};

using KafkaSenderCallbackPtr = std::shared_ptr<KafkaSenderCallback>;
using KafkaProducerPtr = std::shared_ptr<RdKafka::Producer>;

struct KafkaProducerStruct
{
    KafkaProducerPtr producer;
    KafkaSenderCallbackPtr callback;
};

class KafkaSender {
public:
    static KafkaSender* Instance();

    bool PushQueue(LoggroupEntry * data);

    bool CreateKafkaProducer(std::string & brokers, std::string & username, std::string & password);

    ~KafkaSender();

    bool Send(std::vector<PipelineEventGroup> & eventGroupList, size_t logSize, const FlusherKafka * flusherKafka);

    LogstoreFeedBackInterface* GetSenderFeedBackInterface();
    void SetFeedBackInterface(LogstoreFeedBackInterface* pProcessInterface);

    void SetQueueUrgent();

    bool FlushOut(int32_t time_interval_in_mili_seconds);

    size_t GetSendingCount();
private:
    KafkaSender();

    void DaemonSender();

    bool IsFlush();
    void SetFlush();
    void ResetFlush();

    std::atomic_int mLastDaemonRunTime{0};
    std::atomic_int mLastSendDataTime{0};

    std::atomic_int sendFailedCount{0};

    LogstoreKafkaSenderQueue<KafkaSenderQueueParam> mSenderQueue;

    volatile bool mFlushLog;

    std::mutex kafkaProducersMutex;
    std::unordered_map<int64_t, KafkaProducerStruct> kafkaProducers;
};

} 
