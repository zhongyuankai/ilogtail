#include "flusher/KafkaSender.h"
#include "flusher/KafkaAggregator.h"
#include "app_config/AppConfig.h"
#include "common/Thread.h"
#include "common/HashUtil.h"
#include "common/TimeUtil.h"
#include "common/Flags.h"
#include "application/Application.h"
#include "monitor/Monitor.h"
#include "logger/Logger.h"


DEFINE_FLAG_INT32(kafka_send_batch_num_messages, "kafka producer parameter.", 10000);
DEFINE_FLAG_INT32(kafka_send_message_max_bytes, "kafka producer parameter.", 52428800);
DEFINE_FLAG_INT32(kafka_send_queue_buffering_max_kbytes, "kafka producer parameter.", 2097152);
DEFINE_FLAG_INT32(kafka_send_queue_buffering_max_messages, "kafka producer parameter.", 100000);
DEFINE_FLAG_INT32(kafka_send_queue_buffering_max_ms, "kafka producer parameter.", 200);

namespace logtail {

void KafkaSenderCallback::dr_cb(RdKafka::Message & message) {
    if (!message.msg_opaque()) {
        return;
    }
    LoggroupEntry * data = static_cast<LoggroupEntry *>(message.msg_opaque());
    if (message.err()) {
        LOG_ERROR(sLogger, ("kafka message delivery failed", message.errstr())("topic", message.topic_name())(
            "len", message.len())("kafkaProducerKey", data->mKafkaProducerKey)("logstoreKey", data->mLogstoreKey)(
            "offset", data->ConvertFileOffsetToString()));
        KafkaSender::Instance()->OnSendDone(data, KafkaSendInfo::SendResult_SendFail);
        ++mSendFailedCount;
    } else {
        KafkaSender::Instance()->OnSendDone(data, KafkaSendInfo::SendResult_OK);
    }
}

KafkaSender::KafkaSender() {
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
    LOG_INFO(sLogger, ("Set kafka sender queue param depend value", concurrencyCount));
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


void KafkaSender::DaemonSender(int64_t kafkaProducerKey) {
    int32_t lastUpdateMetricTime = time(NULL);

    KafkaAggregator * aggregator = KafkaAggregator::GetInstance();

    KafkaProducerPtr producer;
    KafkaSenderCallbackPtr callback;
    ThreadFlagsPtr threadFlags;
    int32_t threadNo;
    {
        std::lock_guard<std::mutex> lock(kafkaProducersMutex);
        if (auto it = kafkaProducers.find(kafkaProducerKey); it != kafkaProducers.end()) {
            auto & entry = it->second;
            producer = entry->mProducer;
            callback = entry->mCallback;
            threadFlags = entry->mThreadFlags;
            threadNo = entry->mThreadNo;
        } else {
            LOG_ERROR(sLogger, ("Kafka SendThread", "exit")("This is a bug, Kafka producer is NULL, KafkaProducerKey", kafkaProducerKey)("threadNo", threadNo));
            return;
        }
    }

    LOG_INFO(sLogger, ("Kafka SendThread", "start")("ThreaNo", threadNo)("KafkaProducerKey", kafkaProducerKey));
    std::vector<LoggroupEntry *> logGroupToSend;
    while (true) {
        int32_t curTime = time(NULL);
        if (curTime - lastUpdateMetricTime >= 40) {
            if (threadNo != mMetricThreadNo.load()) {
                lastUpdateMetricTime = curTime;
                continue;
            }

            static LogtailMonitor * sMonitor = LogtailMonitor::GetInstance();

            sMonitor->UpdateMetric("send_tps", 1.0 * mSendBufferCount.load() / (curTime - lastUpdateMetricTime));
            sMonitor->UpdateMetric("send_bytes_ps", 1.0 * mSendBufferBytes.load() / (curTime - lastUpdateMetricTime));
            sMonitor->UpdateMetric("send_lines_ps", 1.0 * mSendLines.load() / (curTime - lastUpdateMetricTime));
            sMonitor->UpdateMetric("send_avg_delay_ms", 1.0 * mSendTime.load() / ((curTime - lastUpdateMetricTime) * 1000));
            lastUpdateMetricTime = curTime;
            mSendBufferCount.store(0);
            mSendLines.store(0);
            mSendBufferBytes.store(0);
            mSendTime.store(0);

            // update process queue status
            int32_t invalidCount = 0;
            int32_t totalCount = 0;
            int32_t queueSize = 0;

            mSenderQueue.GetStatus(
                invalidCount, totalCount, queueSize);
            sMonitor->UpdateMetric("send_queue_full", invalidCount);
            sMonitor->UpdateMetric("send_queue_total", totalCount);
            sMonitor->UpdateMetric("send_queue_size", queueSize);
            sMonitor->UpdateMetric("send_error_count", mSendErrorCount.load());
            sMonitor->UpdateMetric("send_error_queue_full_count", mSendErrorQueueFullCount.load());
            sMonitor->UpdateMetric("push_queue_failed_count", mPushQueueFailedCount.load());
            mSendErrorQueueFullCount.store(0);

            size_t sendFailedCount = 0;
            {
                std::lock_guard<std::mutex> lock(kafkaProducersMutex);
                for (auto it = kafkaProducers.begin(); it != kafkaProducers.end(); ++it) {
                    auto & entry = it->second;
                    sendFailedCount += entry->mCallback->getSendFailedCount();
                }
            }
            sMonitor->UpdateMetric("send_failed_count", sendFailedCount);
        }

        mSenderQueue.Wait(kafkaProducerKey, 1000);

        if (Application::GetInstance()->IsExiting() || threadFlags->mInteruptFlag || threadFlags->mFlushLog) {
            mSenderQueue.PopAllItemByKafkaProducerKey(logGroupToSend, kafkaProducerKey, curTime);
        } else {
            mSenderQueue.CheckAndPopAllItemByKafkaProducerKey(logGroupToSend, kafkaProducerKey, curTime);
        }

        mSendBufferCount += logGroupToSend.size();
        size_t sendBufferBytes = 0;
        size_t sendLines = 0;
        size_t sendErrorCount = 0;
        size_t sendErrorQueueFullCount = 0;
        uint64_t startTime = GetCurrentTimeInMilliSeconds();
        for (std::vector<LoggroupEntry *>::iterator itr = logGroupToSend.begin(); itr != logGroupToSend.end(); ++itr) {
            LoggroupEntry * data = *itr;

            KafkaSendInfo::SendResult sendRst = KafkaSendInfo::SendResult_Sending;
            mSenderQueue.OnLoggroupSendDone(data, sendRst);
            {
                WriteBufferPtr logData = std::move(data->mLogData);

                while (true)
                {
                    RdKafka::ErrorCode err = producer->produce(
                        data->mTopic,
                        RdKafka::Topic::PARTITION_UA,
                        RdKafka::Producer::RK_MSG_COPY, /* Copy payload */
                        const_cast<char *>(logData->ToString()), logData->Size(),    /* Value */
                        NULL, 0,    /* Key */
                        0,  /* Timestamp (defaults to current time) */ 
                        NULL,   /* Message headers, if any */
                        data    /* Per-message opaque value passed to delivery report */
                    );

                    if (err == RdKafka::ERR__QUEUE_FULL) {
                        producer->poll(100);   /*block for max 1000ms*/
                        ++sendErrorQueueFullCount;
                        continue;
                    }

                    if (err != RdKafka::ERR_NO_ERROR) {
                        LOG_ERROR(sLogger, ("Failed to send kafka", RdKafka::err2str(err))("log lines", data->mLogLines)("topic", data->mTopic)(
                            "logstoreKey", data->mLogstoreKey)("offset", data->ConvertFileOffsetToString()));
                        ++sendErrorCount;
                        sendRst = KafkaSendInfo::SendResult_OtherFail;
                    }

                    producer->poll(0);
                    break;
                }

                sendBufferBytes += logData->Size();
                sendLines += data->mLogLines;
            }

            if (sendRst == KafkaSendInfo::SendResult_OtherFail) {
                mSenderQueue.OnLoggroupSendDone(data, sendRst);
            }
        }
        logGroupToSend.clear();

        mSendTime += GetCurrentTimeInMilliSeconds() - startTime;
        mSendBufferBytes += sendBufferBytes;
        mSendLines += sendLines;
        mSendErrorCount += sendErrorCount;
        mSendErrorQueueFullCount += sendErrorQueueFullCount;

        if (threadFlags->mFlushLog && aggregator->IsMergeMapEmpty(kafkaProducerKey) && mSenderQueue.GetSize(kafkaProducerKey) == 0) {
            producer->flush(20 * 1000);

            ReadWriteBarrier();
            threadFlags->mFlushLog = false;
        }

        if (threadFlags->mInteruptFlag) {
            LOG_INFO(sLogger, ("Kafka SendThread", "prepare exit")("ThreaNo", threadNo)("KafkaProducerKey", kafkaProducerKey));
            if (!aggregator->IsMergeMapEmpty(kafkaProducerKey) || mSenderQueue.GetSize(kafkaProducerKey) != 0) {
                continue;
            }
            producer->flush(20 * 1000);
            break;
        }

        producer->poll(10);
    }

    if (threadNo == mMetricThreadNo.load()) {
        std::lock_guard<std::mutex> lock(kafkaProducersMutex);
        if (kafkaProducers.empty()) {
            mMetricThreadNo.store(mThreadNo);
        } else {
            auto & entry = kafkaProducers.begin()->second;
            mMetricThreadNo.store(entry->mThreadNo);
        }
        LOG_INFO(sLogger, ("Kafka SendThread switch metric thread", mMetricThreadNo.load()));
    }
    LOG_INFO(sLogger, ("Kafka SendThread", "exit")("ThreaNo", threadNo)("KafkaProducerKey", kafkaProducerKey));
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
                         ToString(data->mLogData->Size()))("topic", data->mTopic));
        }
        usleep(10 * 1000);
    }
    ++mPushQueueFailedCount;
    LOG_ERROR(sLogger,
              ("push file data into queue fail, discard log lines",
               data->mLogLines)("topic", data->mTopic));
    delete data;
    return false;
}

bool KafkaSender::CreateKafkaProducer(FlusherKafka * flusherKafka) {
    std::lock_guard<std::mutex> lock(kafkaProducersMutex);

    int64_t kafkaProducerKey = flusherKafka->kafkaProducerKey;
    auto it = kafkaProducers.find(kafkaProducerKey);
    if (it != kafkaProducers.end()) {
        it->second->mTaskCount++;
        return true;
    }

    auto conf = std::shared_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    std::vector<std::pair<std::string, std::string>> confs = {
        { "bootstrap.servers", flusherKafka->brokers },
        { "security.protocol", "SASL_PLAINTEXT" },
        { "sasl.mechanism", "PLAIN" },
        { "sasl.username", flusherKafka->username },
        { "sasl.password", flusherKafka->password },
        { "compression.codec", "lz4" },
        { "batch.num.messages", ToString(INT32_FLAG(kafka_send_batch_num_messages)) },
        { "message.max.bytes", ToString(INT32_FLAG(kafka_send_message_max_bytes)) },
        { "queue.buffering.max.kbytes", ToString(INT32_FLAG(kafka_send_queue_buffering_max_kbytes)) },
        { "queue.buffering.max.messages", ToString(INT32_FLAG(kafka_send_queue_buffering_max_messages)) },
        { "queue.buffering.max.ms", ToString(INT32_FLAG(kafka_send_queue_buffering_max_ms)) },
    };

    std::string errstr;
    for (const auto & [key, value] : confs) {
        if (conf->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR(sLogger, ("set producer config failed, key", key)("value", value)("error", errstr));
            return false;
        }
    }

    KafkaSenderCallbackPtr callback = std::make_shared<KafkaSenderCallback>();

    if (conf->set("dr_cb", callback.get(), errstr) != RdKafka::Conf::CONF_OK) {
        LOG_ERROR(sLogger, ("set producer callback failed, key", "dr_cb"));
    }

    auto producer = KafkaProducerPtr(RdKafka::Producer::create(conf.get(), errstr));
    if (!producer) {
        LOG_ERROR(sLogger, ("Failed to create producer", errstr));
        return false;
    }

    std::shared_ptr<KafkaProducerEntry> entry = std::make_shared<KafkaProducerEntry>(producer, callback);
    kafkaProducers.emplace(kafkaProducerKey, entry);
    ThreadPtr thread = CreateThread([this, kafkaProducerKey]() { DaemonSender(kafkaProducerKey); });
    entry->setThread(thread, mThreadNo);
    mThreadNo++;
    return true;
}

void KafkaSender::RemoveKafkaProducer(int64_t kafkaProducerKey) {
    std::lock_guard<std::mutex> lock(kafkaProducersMutex);
    auto it = kafkaProducers.find(kafkaProducerKey);
    if (it == kafkaProducers.end()) {
        return;
    }

    auto & entry = it->second;
    if (--entry->mTaskCount > 0) {
        return;
    }
    needClearThead = true;
}

void KafkaSender::ClearKafakProducerThread() {
    std::vector<std::shared_ptr<KafkaProducerEntry>> entrys;
    {
        std::lock_guard<std::mutex> lock(kafkaProducersMutex);
        if (!needClearThead) {
            return;
        }
        for (auto it = kafkaProducers.begin(); it != kafkaProducers.end();) {
            auto & entry = it->second;
            if (entry->mTaskCount == 0) {
                entrys.push_back(entry);
                it = kafkaProducers.erase(it);
                needClearThead = false;
            } else {
                ++it;
            }
        }
    }

    for (const auto & entry : entrys) {
        LOG_INFO(sLogger, ("Interrupt kafka send thread, threadNo", entry->mThreadNo));
        ReadWriteBarrier();
        entry->mThreadFlags->mInteruptFlag = true;
    }

    mSenderQueue.Signal();
}

bool KafkaSender::FlushOut(int32_t time_interval_in_mili_seconds) {
    static KafkaAggregator * aggregator = KafkaAggregator::GetInstance();
    aggregator->ForceFlushBuffer();
    SetFlush();
    for (int i = 0; i < time_interval_in_mili_seconds / 100; ++i) {
        mSenderQueue.Signal();
        if (!IsFlush()) {
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
    std::lock_guard<std::mutex> lock(kafkaProducersMutex);
    for (auto it = kafkaProducers.begin(); it != kafkaProducers.end(); ++it) {
        auto & entry = it->second;
        if (entry->mThreadFlags->mFlushLog) {
            return true;
        }
    }
    return false;
}

void KafkaSender::SetFlush() {
    std::vector<ThreadFlagsPtr> flags;
    {
        std::lock_guard<std::mutex> lock(kafkaProducersMutex);
        for (auto it = kafkaProducers.begin(); it != kafkaProducers.end(); ++it) {
            auto & entry = it->second;
            flags.push_back(entry->mThreadFlags);
        }
    }
    for (const auto & flag : flags) {
        ReadWriteBarrier();
        flag->mFlushLog = true;
    }
}

void KafkaSender::ResetFlush() {
    std::vector<ThreadFlagsPtr> flags;
    {
        std::lock_guard<std::mutex> lock(kafkaProducersMutex);
        for (auto it = kafkaProducers.begin(); it != kafkaProducers.end(); ++it) {
            auto & entry = it->second;
            flags.push_back(entry->mThreadFlags);
        }
    }
    for (const auto & flag : flags) {
        ReadWriteBarrier();
        flag->mFlushLog = false;
    }
}

void KafkaSender::SetQueueUrgent() {
    mSenderQueue.SetUrgent();
}

void KafkaSender::OnSendDone(LoggroupEntry * data, KafkaSendInfo::SendResult sendRst) {
    mSenderQueue.OnLoggroupSendDone(data, sendRst);
}

LoggroupOffsetMap KafkaSender::GetSendFailedOffsets() {
    return mSenderQueue.GetSendFailedOffsets();
}

bool KafkaSender::RegisterKafkaFlusher(FlusherKafka * flusherKafka) {
    if (!CreateKafkaProducer(flusherKafka)) {
        LOG_ERROR(sLogger, ("failed to create kafka producer, configName", flusherKafka->configName)("logstoreKey", flusherKafka->logstoreKey));
        return false;
    }

    KafkaAggregator::GetInstance()->RegisterFlusher(flusherKafka);
    mSenderQueue.CreateKafkaSendQueue(flusherKafka->GetLogstoreKey(), flusherKafka->GetKafkaProducerkey());
    LOG_INFO(sLogger, ("register kafka flusher successfully, configName", flusherKafka->configName)(
        "logstoreKey", flusherKafka->logstoreKey)("kafkaProducerKey", flusherKafka->kafkaProducerKey));
    return true;
}

void KafkaSender::RemoveKafkaFlusher(FlusherKafka * flusherKafka, bool removeQueue) {
    RemoveKafkaProducer(flusherKafka->kafkaProducerKey);
    if (removeQueue) {
        KafkaAggregator::GetInstance()->RemoveFlusher(flusherKafka);
    }
    LOG_INFO(sLogger, ("remove kafka flusher successfully, configName", flusherKafka->configName)(
        "logstoreKey", flusherKafka->logstoreKey)("kafkaProducerKey", flusherKafka->kafkaProducerKey)("removeQueue", removeQueue));
}

KafkaSendStatistics KafkaSender::GetSenderStatistics(const LogstoreFeedBackKey & key) {
    return mSenderQueue.GetSenderStatistics(key);
}

} // namespace logtail
