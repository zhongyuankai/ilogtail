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
    const FlusherKafka * mFlusherKafka = nullptr;

    time_t mLastSendTime;
    std::vector<PipelineEventGroup> mEventGroupList;
    size_t mLogSize;
    std::mutex mMutex;

    MergeEntry(FlusherKafka * flusherKafka)
        : mFlusherKafka(flusherKafka)
        , mLastSendTime(time(nullptr)) {
    }

    void merge(std::vector<PipelineEventGroup> & eventGroupList) {
        for (auto & eventGroup : eventGroupList) {
            mLogSize += eventGroup.EventGroupSizeBytes();
            mEventGroupList.emplace_back(std::move(eventGroup));
        }
        eventGroupList.clear();
    }

    void clear() {
        mLogSize = 0;
        mEventGroupList.clear();
        mLastSendTime = time(nullptr);
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
    void RemoveFlusher(FlusherKafka * flusherKafka);

    bool Add(std::vector<PipelineEventGroup> & eventGroupList, size_t logSize, const FlusherKafka * flusherKafka);

    bool ForceFlushBuffer();

private:
    KafkaAggregator() = default;
    ~KafkaAggregator() = default;

    bool SendData(std::vector<PipelineEventGroup> & eventGroupList, MergeEntryPtr entry);

    std::unordered_map<LogstoreFeedBackKey, MergeEntryPtr> mMergeMap;
};

} // namespace logtail
