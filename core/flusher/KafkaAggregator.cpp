#include "flusher/KafkaAggregator.h"
#include "Constants.h"
#include "flusher/KafkaSender.h"
#include "common/Flags.h"
#include "models/StringView.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;


DEFINE_FLAG_INT32(max_log_group_size, "Maximum size of a message sent to Kafka.", 2 * 1024 * 1024);
DEFINE_FLAG_INT32(batch_kafka_send_interval, "batch kafka sender interval (second)(default 10)", 10);

namespace logtail {

using NodeType = sonic_json::Node;
using WriteBuffer = sonic_json::WriteBuffer;


void KafkaAggregator::RegisterFlusher(FlusherKafka * flusherKafka) {
    if (auto it = mMergeMap.find(flusherKafka->GetLogstoreKey()); it != mMergeMap.end()) {
        it->second->mFlusherKafka = flusherKafka;
    } else {
        mMergeMap[flusherKafka->GetLogstoreKey()] = std::make_shared<MergeEntry>(flusherKafka);
    }
    LOG_INFO(sLogger, ("KafkaAggregator register flusher, logstoreKey", flusherKafka->GetLogstoreKey())("configName", flusherKafka->configName));
}

void KafkaAggregator::RemoveFlusher(FlusherKafka * flusherKafka) {
    mMergeMap.erase(flusherKafka->GetLogstoreKey());
    LOG_INFO(sLogger, ("KafkaAggregator remove flusher, logstoreKey", flusherKafka->GetLogstoreKey())("configName", flusherKafka->configName));
}

bool KafkaAggregator::Add(std::vector<PipelineEventGroup> & eventGroupList, size_t logSize, const FlusherKafka * flusherKafka) {
    MergeEntryPtr mergeEntry = nullptr;
    std::vector<PipelineEventGroup> sendList;

    auto it = mMergeMap.find(flusherKafka->GetLogstoreKey());
    if (it == mMergeMap.end()) {
        LOG_ERROR(sLogger, ("This is a bug, MergeEntry does not exist, logstoreKey", flusherKafka->GetLogstoreKey())("configName", flusherKafka->configName));
        return false;
    } else {
        MergeEntryPtr entry = it->second;
        std::lock_guard<std::mutex> lock(entry->mMutex);
        if (entry->mLogSize + logSize > static_cast<size_t>(INT32_FLAG(max_log_group_size))) {
            if (entry->mLogSize != 0) {
                sendList.insert(sendList.end(), std::make_move_iterator(entry->mEventGroupList.begin()), std::make_move_iterator(entry->mEventGroupList.end()));
                entry->clear();
                mergeEntry = entry;
            }
            entry->merge(eventGroupList);
        } else {
            entry->merge(eventGroupList);
            return true;
        }
    }

    if (mergeEntry != nullptr && !sendList.empty()) {
        return SendData(sendList, mergeEntry);
    }
    return true;
}

bool KafkaAggregator::SendData(std::vector<PipelineEventGroup> & eventGroupList, MergeEntryPtr & entry) {
    sonic_json::Document doc;
    auto & alloc = doc.GetAllocator();
    doc.SetArray();

    LoggroupEntry * data = new LoggroupEntry(
                entry->mFlusherKafka->topic,
                entry->mFlusherKafka->logstoreKey,
                entry->mFlusherKafka->kafkaProducerKey);

    int32_t logLines = 0;
    for (const auto & eventGroup : eventGroupList) {
        auto path = fs::path(eventGroup.GetTag(LOG_RESERVED_KEY_PATH).to_string());
        std::string inode = eventGroup.GetTag(LOG_RESERVED_KEY_INODE).to_string();

        int64_t currentTime = static_cast<int64_t>(time(nullptr)) * 1000;

        std::string parentPath = path.parent_path();
        std::string logName = path.filename();

        int64_t offset = -1;

        for (const auto & event : eventGroup.GetEvents()) {
            if (!event.Is<LogEvent>()) {
                continue;
            }

            auto & logEvent = event.Cast<LogEvent>();

            int64_t logTime = static_cast<int64_t>(logEvent.GetTimestamp()) * 1000;
            StringView content;
            std::string fileOffsetStr;
            int64_t fileOffset = -1;

            for (const auto & kv : logEvent) {
                if (kv.first == DEFAULT_CONTENT_KEY) {
                    content = kv.second;
                } else if (kv.first == LOG_RESERVED_KEY_FILE_OFFSET) {
                    fileOffsetStr = kv.second.to_string();

                    try {
                        fileOffset = StringTo<int64_t>(fileOffsetStr);
                        if (offset == -1) {
                            offset = fileOffset;
                        } else if (fileOffset < offset) {
                            offset = fileOffset;
                        }
                    } catch (...) {
                    }
                }
            }

            logLines++;

            NodeType node;
            node.SetObject();

            node.AddMember(DEFAULT_CONTENT_KEY, NodeType(content.data(), content.size()), alloc, false);

            static std::string hostNameKey = "hostName";
            node.AddMember(hostNameKey, NodeType(entry->mFlusherKafka->hostName), alloc, false);

            static std::string uniqueKey = "uniqueKey";
            node.AddMember(uniqueKey, NodeType(inode + "_" + fileOffsetStr, alloc), alloc, false);

            static std::string originalAppNameKey = "originalAppName";
            node.AddMember(originalAppNameKey, NodeType(entry->mFlusherKafka->originalAppName), alloc, false);

            static std::string odinLeafKey = "odinLeaf";
            node.AddMember(odinLeafKey, NodeType(entry->mFlusherKafka->odinLeaf), alloc, false);

            static std::string logTimeKey = "logTime";
            node.AddMember(logTimeKey, NodeType(logTime), alloc, false);

            static std::string logIdKey = "logId";
            node.AddMember(logIdKey, NodeType(entry->mFlusherKafka->logId), alloc, false);

            static std::string appNameKey = "appName";
            node.AddMember(appNameKey, NodeType(entry->mFlusherKafka->appName), alloc, false);

            static std::string queryFromKey = "queryFrom";
            node.AddMember(queryFromKey, NodeType(entry->mFlusherKafka->queryFrom), alloc, false);

            static std::string logNameKey = "logName";
            node.AddMember(logNameKey, NodeType(logName, alloc), alloc, false);

            static std::string isServiceKey = "isService";
            node.AddMember(isServiceKey, NodeType(entry->mFlusherKafka->isService), alloc, false);

            static std::string pathIdKey = "pathId";
            node.AddMember(pathIdKey, NodeType(entry->mFlusherKafka->pathId), alloc, false);

            static std::string timestampKey = "timestamp";
            node.AddMember(timestampKey, NodeType(ToString(logTime), alloc), alloc, false);

            static std::string collectTimeKey = "collectTime";
            node.AddMember(collectTimeKey, NodeType(currentTime), alloc, false);

            static std::string fileKey = "fileKey";
            node.AddMember(fileKey, NodeType(inode, alloc), alloc, false);

            static std::string parentPathKey = "parentPath";
            node.AddMember(parentPathKey, NodeType(parentPath, alloc), alloc, false);

            static std::string offsetKey = "offset";
            node.AddMember(offsetKey, NodeType(fileOffset), alloc, false);

            static std::string DIDIENV_ODIN_SU_Key = "DIDIENV_ODIN_SU";
            node.AddMember(DIDIENV_ODIN_SU_Key, NodeType(entry->mFlusherKafka->DIDIENV_ODIN_SU), alloc, false);

            doc.PushBack(std::move(node), alloc);
        }

        /// 计算数据最小的offset
        if (offset != -1) {
            std::string fileHeadMd5 = eventGroup.GetTag(LOG_RESERVED_KEY_FILE_HEAD_MD5).to_string();
            LoggroupOffsetKey key(entry->mFlusherKafka->logstoreKey);
            key.mFileKey = std::move(inode);
            key.mFileHeadMd5 = std::move(fileHeadMd5);

            auto [it, inserted] = data->mFileOffsets.emplace(key, offset);
            if (!inserted && offset < it->second) {
                it->second = offset;
            }
        }
    }

    if (logLines == 0) {
        delete data;
        LOG_WARNING(sLogger, ("log group is empty, skip send, configName", entry->mFlusherKafka->configName));
        return true;
    }

    doc.Serialize(*(data->mLogData));
    data->mLogLines = logLines;

    static KafkaSender * sender = KafkaSender::Instance();
    return sender->PushQueue(data);
}

bool KafkaAggregator::FlushReadyBuffer() {
    static KafkaSender * sender = KafkaSender::Instance();
    
    std::vector<PipelineEventGroup> sendList;
    time_t currentTime = time(nullptr);
    for (auto it = mMergeMap.begin(); it != mMergeMap.end(); ++it) {
        MergeEntryPtr entry = it->second;
        if (entry->mLastSendTime + INT32_FLAG(batch_kafka_send_interval) > currentTime) {
            if (!sender->GetSenderFeedBackInterface()->IsValidToPush(entry->mFlusherKafka->logstoreKey)) {
                continue;
            }
        }

        {
            std::lock_guard<std::mutex> lock(entry->mMutex);
            if (entry->mLogSize == 0) {
                continue;
            }

            sendList.insert(sendList.end(), std::make_move_iterator(entry->mEventGroupList.begin()), std::make_move_iterator(entry->mEventGroupList.end()));
            entry->clear();
        }

        SendData(sendList, entry);
        sendList.clear();
    }
    return true;
}

bool KafkaAggregator::ForceFlushBuffer() {
    std::vector<PipelineEventGroup> sendList;
    for (auto it = mMergeMap.begin(); it != mMergeMap.end(); ++it) {
        MergeEntryPtr entry = it->second;
        {
            std::lock_guard<std::mutex> lock(entry->mMutex);
            if (entry->mLogSize == 0) {
                continue;
            }

            sendList.insert(sendList.end(), std::make_move_iterator(entry->mEventGroupList.begin()), std::make_move_iterator(entry->mEventGroupList.end()));
            entry->clear();
        }

        SendData(sendList, entry);
        sendList.clear();
    }
    return true;
}

bool KafkaAggregator::IsMergeMapEmpty(int64_t kafkaProducerKey) {
    for (auto it = mMergeMap.begin(); it != mMergeMap.end(); ++it) {
        MergeEntryPtr entry = it->second;
        std::lock_guard<std::mutex> lock(entry->mMutex);
        if (entry->mFlusherKafka->kafkaProducerKey == kafkaProducerKey && entry->mLogSize != 0) {
            return false;
        }
    }
    return true;
}

bool KafkaAggregator::IsMergeMapEmpty() {
    for (auto it = mMergeMap.begin(); it != mMergeMap.end(); ++it) {
        MergeEntryPtr entry = it->second;
        std::lock_guard<std::mutex> lock(entry->mMutex);
        if (entry->mLogSize != 0) {
            return false;
        }
    }
    return true;
}

}
