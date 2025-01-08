#pragma once

#include "plugin/interface/Flusher.h"
#include "flusher/KafkaAggregator.h"
#include "common/LogstoreFeedbackKey.h"


namespace logtail {

class FlusherKafka : public Flusher {
friend class KafkaAggregator;
friend class KafkaSender;
public:
    static const std::string sName;

    FlusherKafka() {};

    const std::string& Name() const override { return sName; }
    bool Init(const Json::Value& config, Json::Value& optionalGoPipeline) override;
    bool Start() override;
    bool Stop(bool isPipelineRemoving) override;

    const LogstoreFeedBackKey & GetLogstoreKey() const { return logstoreKey; }
    int64_t GetKafkaProducerkey() const { return kafkaProducerKey; }

private:

    std::string brokers;
    std::string topic;
    std::string username;
    std::string password;

    std::string hostName;
	std::string originalAppName;
	std::string odinLeaf;
	int32_t logId = -1;
	std::string appName;
	std::string queryFrom;
	int32_t isService = 0;
	std::string DIDIENV_ODIN_SU;
	int32_t pathId = -1;

    LogstoreFeedBackKey logstoreKey = 0;
    int64_t kafkaProducerKey = 0;
    std::string configName;
};

} // namespace logtail
