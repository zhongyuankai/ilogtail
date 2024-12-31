/*
 * Copyright 2024 iLogtail Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>

#include "common/DevInode.h"
#include "checkpoint/CheckPoint.h"

namespace fs = std::filesystem; 

namespace logtail {


class CheckPointManagerV3 {
public:
    using OffsetMap = std::unordered_map<std::string, std::unordered_map<std::string, CheckPointPtr>>;

private:
    OffsetMap mOffsetMap;
    int32_t mLastCheckTime;
    int32_t mLastDumpTime;
    int32_t mLoadVersion;
    int32_t mReaderCount;

    const std::string DDCLOUD_ERROR_TARGET_TAG = "NO_HOST_MAP_DIR";
    const std::string TMP_SUFFIX = ".swanagent.tmp";
    const std::string DEL_SUFFIX = ".swanagent.del";
    const std::string SEPARATORCHAR = "###";
    const int64_t _2WEEK = 2 * 7 * 24 * 60 * 60;

    const std::string LOGID_STR            = "logId";
    const std::string PATHID_STR           = "pathId";
    const std::string OFFSET_STR           = "offSet";
    const std::string TIMESTAMP_STR        = "timeStamp";
    const std::string PARENTPATH_STR       = "parentPath";
    const std::string FILENAME_STR         = "fileName";
    const std::string FILE_KEY             = "fileKey";
    const std::string LAST_MODIFY_TIME_STR = "lastModifyTime";
    const std::string FILE_HEAD_MD5        = "fileHeadMd5";

    CheckPointManagerV3()
        : mLastCheckTime(time(NULL)), mLastDumpTime(time(NULL)), mLoadVersion(0), mReaderCount(0) {}
    
public:
    void AddCheckPoint(const CheckPointPtr & checkPointPtr);
    void DeleteCheckPoint(DevInode devInode, const std::string& logMode4Path);
    void LoadCheckPoint();
    void LoadDirCheckPoint(fs::path dirPath);
    bool DumpCheckPointToLocal();
    int32_t GetReaderCount();
    bool GetCheckPoint(DevInode devInode, const std::string & logMode4Path, CheckPointPtr& checkPointPtr);
    std::unordered_map<std::string, CheckPointPtr> & GetCheckPoints(const std::string & logMode4Path);
    void RemoveAllCheckPoint();
    void CheckTimeoutCheckPoint();
    bool NeedDump(int32_t curTime);
    void ResetLastDumpTime();

    static CheckPointManagerV3* Instance() {
        static CheckPointManagerV3 checkPointManager;
        return &checkPointManager;
    }

    static bool CheckPointCmpByUpdateTime(const CheckPoint* left, const CheckPoint* right) {
        return left->mLastUpdateTime > right->mLastUpdateTime;
    }
};

} // namespace logtail
