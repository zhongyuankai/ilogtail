#include "flusher/KafkaSender.h"
#include "flusher/KafkaAggregator.h"
#include "app_config/AppConfig.h"
#include "common/Thread.h"
#include "common/HashUtil.h"
#include "application/Application.h"
#include "monitor/Monitor.h"
#include "logger/Logger.h"
#include "common/TimeUtil.h"


namespace logtail {

void KafkaSenderCallback::dr_cb(RdKafka::Message & message) {
    if (message.err()) {
        LOG_ERROR(sLogger, ("Message delivery failed", message.errstr())("topic", message.topic_name())("len", message.len()));
        ++sendFailedCount;
    }
}

KafkaSender::KafkaSender() {
    mFlushLog = false;
    size_t concurrencyCount = (size_t)AppConfig::GetInstance()->GetSendRequestConcurrency();
    if (concurrencyCount < 10) {
        concurrencyCount = 10;
    }
    if (concurrencyCount > 50) {
        concurrencyCount = 50;
    }
    mSenderQueue.SetParam((size_t)(concurrencyCount * 1.5), (size_t)(concurrencyCount * 2), 200);
    size_t maxQueueSize = (size_t)AppConfig::GetInstance()->GetSendMaxQueueSize();
    mSenderQueue.SetMaxQueueSize(maxQueueSize);
    LOG_INFO(sLogger, ("Set sender queue param depend value", concurrencyCount));

    new Thread(std::bind(&KafkaSender::DaemonSender, this)); 
}

KafkaSender* KafkaSender::Instance() {
    static KafkaSender* senderPtr = new KafkaSender();
    return senderPtr;
}

bool KafkaSender::Send(std::vector<PipelineEventGroup> & eventGroupList, size_t logSize, const FlusherKafka * flusherKafka) {
    static KafkaAggregator * aggregator = KafkaAggregator::GetInstance();
    return aggregator->Add(eventGroupList, logSize, flusherKafka);
}

void KafkaSender::SetFeedBackInterface(LogstoreFeedBackInterface* pProcessInterface) {
    mSenderQueue.SetFeedBackObject(pProcessInterface);
}

LogstoreFeedBackInterface* KafkaSender::GetSenderFeedBackInterface() {
    return (LogstoreFeedBackInterface*)&mSenderQueue;
}


void KafkaSender::DaemonSender() {
    LOG_INFO(sLogger, ("SendThread", "start"));
    int32_t lastUpdateMetricTime = time(NULL);
    int32_t sendBufferCount = 0;
    size_t sendBufferBytes = 0;
    size_t sendLines = 0;
    size_t sendErrorCount = 0;
    size_t sendErrorQueueFullCount = 0;
    uint64_t sendTime = 0;
    mLastDaemonRunTime = lastUpdateMetricTime;
    mLastSendDataTime = lastUpdateMetricTime;
    KafkaAggregator * aggregator = KafkaAggregator::GetInstance();
    while (true) {
        int32_t curTime = time(NULL);
        if (curTime - lastUpdateMetricTime >= 40) {
            static auto sMonitor = LogtailMonitor::GetInstance();

            sMonitor->UpdateMetric("send_tps", 1.0 * sendBufferCount / (curTime - lastUpdateMetricTime));
            sMonitor->UpdateMetric("send_bytes_ps", 1.0 * sendBufferBytes / (curTime - lastUpdateMetricTime));
            // sMonitor->UpdateMetric("send_net_bytes_ps", 1.0 * sendNetBodyBytes / (curTime - lastUpdateMetricTime));
            sMonitor->UpdateMetric("send_lines_ps", 1.0 * sendLines / (curTime - lastUpdateMetricTime));
            sMonitor->UpdateMetric("send_avg_delay_ms", 1.0 * sendTime / ((curTime - lastUpdateMetricTime) * 1000));
            lastUpdateMetricTime = curTime;
            sendBufferCount = 0;
            sendLines = 0;
            sendBufferBytes = 0;
            sendTime = 0;

            // update process queue status
            int32_t invalidCount = 0;
            int32_t totalCount = 0;
            int32_t queueSize = 0;

            mSenderQueue.GetStatus(
                invalidCount, totalCount, queueSize);
            sMonitor->UpdateMetric("send_queue_full", invalidCount);
            sMonitor->UpdateMetric("send_queue_total", totalCount);
            sMonitor->UpdateMetric("send_queue_size", queueSize);
            sMonitor->UpdateMetric("send_error_count", sendErrorCount);
            sMonitor->UpdateMetric("send_error_queue_full_count", sendErrorQueueFullCount);
            sendErrorQueueFullCount = 0;
            sMonitor->UpdateMetric("send_failed_count", sendFailedCount.load());
            sMonitor->UpdateMetric("push_queue_failed_count", pushQueueFailedCount.load());
        }

        mSenderQueue.Wait(1000);

        bool singleBatchMapFull = false;

        static std::vector<LoggroupEntry*> logGroupToSend;
        mSenderQueue.PopAllItem(logGroupToSend);

        mLastDaemonRunTime = curTime;

        sendBufferCount += logGroupToSend.size();
        uint64_t startTime = GetCurrentTimeInMilliSeconds();
        for (std::vector<LoggroupEntry*>::iterator itr = logGroupToSend.begin(); itr != logGroupToSend.end(); ++itr) {
            LoggroupEntry * data = *itr;

            mLastSendDataTime = curTime;

            KafkaProducerPtr producer;
            {
                std::lock_guard<std::mutex> lock(kafkaProducersMutex);
                if (auto it = kafkaProducers.find(data->mKafkaProducerKey); it != kafkaProducers.end()) {
                    producer = it->second.producer;
                } else {
                    LOG_ERROR(sLogger, ("This is a bug, Kafka producer is NULL, KafkaProducerKey", data->mKafkaProducerKey));
                    mSenderQueue.RemoveItem(data);
                    continue;
                }
            }

            while (true)
            {
                RdKafka::ErrorCode err = producer->produce(
                    data->mTopic,
                    RdKafka::Topic::PARTITION_UA,
                    RdKafka::Producer::RK_MSG_COPY, /* Copy payload */
                    const_cast<char *>(data->mLogData.ToString()), data->mLogData.Size(),    /* Value */
                    NULL, 0,    /* Key */
                    0,  /* Timestamp (defaults to current time) */ 
                    NULL,   /* Message headers, if any */
                    NULL    /* Per-message opaque value passed to delivery report */
                );

                if (err == RdKafka::ERR__QUEUE_FULL) {
                    producer->poll(100);   /*block for max 1000ms*/
                    ++sendErrorQueueFullCount;
                    continue;
                }

                if (err != RdKafka::ERR_NO_ERROR) {
                    LOG_ERROR(sLogger, ("Failed to send kafka", err));
                    ++sendErrorCount;
                }
                producer->poll(0);
                break;
            }


            sendBufferBytes += data->mLogData.Size();
            sendLines += data->mLogLines;
            mSenderQueue.RemoveItem(data);
        }
        sendTime += GetCurrentTimeInMilliSeconds() - startTime;

        logGroupToSend.clear();

        {
            std::lock_guard<std::mutex> lock(kafkaProducersMutex);
            for (auto it = kafkaProducers.begin(); it != kafkaProducers.end(); ++it) {
                it->second.producer->poll(0);
            }
        }

        if (IsFlush() && aggregator->IsMergeMapEmpty() && mSenderQueue.GetSize() == 0) {
            std::lock_guard<std::mutex> lock(kafkaProducersMutex);
            for (auto it = kafkaProducers.begin(); it != kafkaProducers.end(); ++it) {
                it->second.producer->flush(20 * 1000);
            }
            ResetFlush();
        }
    }
    LOG_INFO(sLogger, ("SendThread", "exit"));
}

bool KafkaSender::PushQueue(LoggroupEntry * data) {
    int32_t tryTime = 0;
    while (tryTime < 1000) {
        if (mSenderQueue.PushItem(data->mLogstoreKey, data)) {
            return true;
        }
        if (tryTime++ == 0) {
            LOG_WARNING(sLogger,
                        ("Push to sender buffer map fail, try again, data size",
                         ToString(data->mLogData.Size()))("topic", data->mTopic));
        }
        usleep(10 * 1000);
    }
    ++pushQueueFailedCount;
    LOG_ERROR(sLogger,
              ("push file data into queue fail, discard log lines",
               data->mLogLines)("topic", data->mTopic));
    delete data;
    return false;
}

bool KafkaSender::CreateKafkaProducer(std::string & brokers, std::string & username, std::string & password) {
    std::string producerKey = brokers + "_" + username + "_" + password;
    int64_t hashKey = HashString(producerKey);

    std::lock_guard<std::mutex> lock(kafkaProducersMutex);
    auto it = kafkaProducers.find(hashKey);
    if (it != kafkaProducers.end()) {
        return true;
    }

    auto conf = std::shared_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    std::vector<std::pair<std::string, std::string>> confs = {
        { "bootstrap.servers", brokers },
        { "security.protocol", "SASL_PLAINTEXT" },
        { "sasl.mechanism", "PLAIN" },
        { "sasl.username", username },
        { "sasl.password", password },
        { "compression.codec", "lz4" },
        { "batch.num.messages", "10000" },
        { "message.max.bytes", "52428800" },
        { "queue.buffering.max.kbytes", "4194304" },
        { "queue.buffering.max.messages", "100000" },
        { "queue.buffering.max.ms", "200" },
    };

    std::string errstr;
    for (const auto & [key, value] : confs) {
        if (conf->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR(sLogger, ("set producer config failed, key", key)("value", value)("error", errstr));
            return false;
        }
    }

    KafkaSenderCallbackPtr callback = std::make_shared<KafkaSenderCallback>(sendFailedCount);

    if (conf->set("dr_cb", callback.get(), errstr) != RdKafka::Conf::CONF_OK) {
        LOG_ERROR(sLogger, ("set producer callback failed, key", "dr_cb"));
    }

    auto producer = KafkaProducerPtr(RdKafka::Producer::create(conf.get(), errstr));
    if (!producer) {
        LOG_ERROR(sLogger, ("Failed to create producer", errstr));
        return false;
    }
    kafkaProducers[hashKey] = {producer, callback};
    return true;
}

bool KafkaSender::FlushOut(int32_t time_interval_in_mili_seconds) {
    static KafkaAggregator* aggregator = KafkaAggregator::GetInstance();
    aggregator->ForceFlushBuffer();
    SetFlush();
    for (int i = 0; i < time_interval_in_mili_seconds / 100; ++i) {
        mSenderQueue.Signal();
        if (!IsFlush()) {
            // double check, fix bug #13758589
            // TODO: this is not necessary, the task of checking whether all data has been flushed should be done in
            // this func, not in Sender thread
            aggregator->ForceFlushBuffer();
            if (aggregator->IsMergeMapEmpty() && mSenderQueue.GetSize() == 0) {
                return true;
            } else {
                SetFlush();
                continue;
            }
        }
        usleep(100 * 1000);
    }
    ResetFlush();
    return false;
}

bool KafkaSender::IsFlush() {
    return mFlushLog;
}

void KafkaSender::SetFlush() {
    ReadWriteBarrier();
    mFlushLog = true;
}

void KafkaSender::ResetFlush() {
    ReadWriteBarrier();
    mFlushLog = false;
}

void KafkaSender::SetQueueUrgent() {
    mSenderQueue.SetUrgent();
}

}
