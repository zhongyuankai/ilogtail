#pragma once

#include "common/LogstoreFeedbackKey.h"
#include "unordered_map"

namespace logtail {

struct LoggroupOffsetKey {
    LogstoreFeedBackKey mLogstoreKey;
    std::string mFileKey;
    std::string mFileHeadMd5;

    LoggroupOffsetKey(const LogstoreFeedBackKey & logstoreKey, const std::string & fileKey, const std::string & fileHeadMd5)
        : mLogstoreKey(logstoreKey)
        , mFileKey(fileKey)
        , mFileHeadMd5(fileHeadMd5) {
    }

    LoggroupOffsetKey(const LogstoreFeedBackKey & logstoreKey)
        : mLogstoreKey(logstoreKey) {
    }

    bool operator==(const LoggroupOffsetKey & other) const {
        return mLogstoreKey == other.mLogstoreKey &&
               mFileKey == other.mFileKey &&
               mFileHeadMd5 == other.mFileHeadMd5;
    }
};

struct LoggroupOffsetKeyHasher {
    std::size_t operator()(const LoggroupOffsetKey & key) const {
        std::size_t h1 = std::hash<int64_t>()(key.mLogstoreKey);
        std::size_t h2 = std::hash<std::string>()(key.mFileKey);
        std::size_t h3 = std::hash<std::string>()(key.mFileHeadMd5);

        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

using LoggroupOffsetMap = std::unordered_map<LoggroupOffsetKey, int64_t, LoggroupOffsetKeyHasher>;

} // namespace logtail
