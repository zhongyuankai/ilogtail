#pragma once

#include "models/PipelineEventGroup.h"
#include "common/LogstoreFeedbackKey.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace logtail {

class FlusherKafka;

struct MergeEntry {
    std::string hostName;
	std::string originalAppName;
	std::string odinLeaf;
	int32_t logId;
	std::string appName;
	std::string queryFrom;
	int32_t isService;
	std::string DIDIENV_ODIN_SU;
	int32_t pathId;

    std::string topic;
    std::string configName;
    LogstoreFeedBackKey logstoreKey;
    int64_t kafkaProducerKey;

    time_t lastSendTime;
    std::vector<PipelineEventGroup> eventGroupList;
    size_t logSize;

    std::mutex mutex;

    MergeEntry(std::string & hostName_,
	        std::string & originalAppName_,
	        std::string & odinLeaf_,
	        int32_t logId_,
	        std::string & appName_,
	        std::string & queryFrom_,
	        int32_t isService_,
	        std::string & DIDIENV_ODIN_SU_,
	        int32_t pathId_,
            std::string & topic_,
            std::string & configName_,
            LogstoreFeedBackKey & logstoreKey_,
            int64_t kafkaProducerKey_)
        : hostName(hostName_)
        , originalAppName(originalAppName_)
	    , odinLeaf(odinLeaf_)
	    , logId(logId_)
	    , appName(appName_)
	    , queryFrom(queryFrom_)
	    , isService(isService_)
	    , DIDIENV_ODIN_SU(DIDIENV_ODIN_SU_)
	    , pathId(pathId_)
        , topic(topic_)
        , configName(configName_)
        , logstoreKey(logstoreKey_)
        , kafkaProducerKey(kafkaProducerKey_)
        , lastSendTime(time(nullptr)) {
    }

    void merge(std::vector<PipelineEventGroup> & list) {
        for (auto & eventGroup : list) {
            logSize += eventGroup.EventGroupSizeBytes();
            eventGroupList.emplace_back(std::move(eventGroup));
        }
        list.clear();
    }

    void clear() {
        logSize = 0;
        eventGroupList.clear();
        lastSendTime = time(nullptr);
    }
};

using MergeEntryPtr = std::shared_ptr<MergeEntry>;

class KafkaAggregator {
public:
    static KafkaAggregator* GetInstance() {
        static KafkaAggregator * instance = new KafkaAggregator();
        return instance;
    }

    bool FlushReadyBuffer();
    bool IsMergeMapEmpty();

    void RegisterFlusher(FlusherKafka * flusherKafka);

    bool Add(std::vector<PipelineEventGroup> & eventGroupList, size_t logSize, const FlusherKafka * flusherKafka);

    bool ForceFlushBuffer();

private:
    KafkaAggregator() = default;
    ~KafkaAggregator() = default;

    bool SendData(std::vector<PipelineEventGroup> & eventGroupList, MergeEntryPtr entry);

    std::unordered_map<std::string, MergeEntryPtr> mMergeMap;
};

} // namespace logtail
