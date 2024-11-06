#include "flusher/FlusherKafka.h"
#include "flusher/KafkaAggregator.h"
#include "flusher/KafkaSender.h"
#include "common/ParamExtractor.h"
#include "common/HashUtil.h"
#include <json/json.h>

#include <numeric>


namespace logtail {

const std::string FlusherKafka::sName = "flusher_kafka_v3";

bool FlusherKafka::Init(const Json::Value& config, Json::Value& optionalGoPipeline) {
    std::string errorMsg;

    if (!GetMandatoryStringParam(config, "Brokers", brokers, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryStringParam(config, "Username", username, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryStringParam(config, "Password", password, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!KafkaSender::Instance()->CreateKafkaProducer(brokers, username, password)) {
        return false;
    }

    std::string key = brokers + "_" + username + "_" + password;
    kafkaProducerKey = HashString(key);

    logstoreKey = mContext->GetLogstoreKey();
    configName = mContext->GetConfigName();

    if (!GetMandatoryStringParam(config, "Topic", topic, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryStringParam(config, "HostName", hostName, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }
    
    if (!GetMandatoryStringParam(config, "OriginalAppName", originalAppName, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryStringParam(config, "OdinLeaf", odinLeaf, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryIntParam(config, "LogId", logId, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryStringParam(config, "QueryFrom", queryFrom, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryIntParam(config, "IsService", isService, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetOptionalStringParam(config, "DIDIENV_ODIN_SU", DIDIENV_ODIN_SU, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    if (!GetMandatoryIntParam(config, "PathId", pathId, errorMsg)) {
        PARAM_ERROR_RETURN(mContext->GetLogger(),
                           mContext->GetAlarm(),
                           errorMsg,
                           sName,
                           mContext->GetConfigName(),
                           mContext->GetProjectName(),
                           mContext->GetLogstoreName(),
                           mContext->GetRegion());
    }

    KafkaAggregator * aggregator = KafkaAggregator::GetInstance();
    aggregator->RegisterFlusher(this);

    return true;
}

bool FlusherKafka::Start() {
    return true;
}

bool FlusherKafka::Stop(bool isPipelineRemoving) {
    return true;
}

}
