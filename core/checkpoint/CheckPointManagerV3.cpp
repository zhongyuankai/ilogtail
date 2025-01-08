// Copyright 2024 iLogtail Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "CheckPointManagerV3.h"

#include "app_config/AppConfig.h"
#include "common/Flags.h"
#include "common/StringTools.h"
#include "file_server/FileDiscoveryOptions.h"
#include "logger/Logger.h"
#include <boost/optional.hpp>

#if defined(__linux__)
DEFINE_FLAG_STRING(dd_check_point_filename, "", "/tmp/logtail_check_point");
#elif defined(_MSC_VER)
DEFINE_FLAG_STRING(check_point_filename, "", "C:\\LogtailData\\logtail_check_point");
#endif
DEFINE_FLAG_INT32(dd_file_check_point_time_out, "seconds", 300);
DEFINE_FLAG_INT32(dd_mem_check_point_time_out, "seconds", 7200);
DEFINE_FLAG_INT32(dd_check_point_check_interval, "default 15 min", 14 * 60);
DEFINE_FLAG_INT32(dd_check_point_version, "now check point version xx.xx.xx such as 0.1.0 is 100", 200);
DEFINE_FLAG_INT32(dd_check_point_dump_interval, "default 15 min", 15 * 60);
DEFINE_FLAG_INT32(dd_check_point_max_count, "max check point count", 100000);
DEFINE_FLAG_INT32(dd_checkpoint_find_max_file_count, "", 1000);


namespace logtail {

std::vector<std::string> listChildren(fs::path & dir) {
    std::vector<std::string> list;
    for (const auto & entry : fs::directory_iterator(dir)) {
        list.push_back(entry.path().filename().string());
    }
    return list;
}

std::vector<std::string> readFileContent(fs::path & path) {
    std::vector<std::string> list;
    try
    {
        std::ifstream in(path);
        if (!in.is_open()) {
            return {};
        }
    
        std::string line;
        while(std::getline(in, line)) {
            list.push_back(line);
        }
        in.close();
    } catch (...) {
        LOG_ERROR(sLogger, ("read file content failed, path", path));
    }
    return list;
}

bool parseJsonString(const std::string & jsonStr, Json::Value & jsonRoot) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::unique_ptr<Json::CharReader> jsonReader(builder.newCharReader());
    std::string jsonParseErrs;
    if (!jsonReader->parse(jsonStr.data(), jsonStr.data() + jsonStr.size(), &jsonRoot, &jsonParseErrs)) {
        LOG_WARNING(sLogger, ("invalid json string", jsonStr)("ParseConfig error", jsonParseErrs));
        return false;
    }
    return true;
}

void CheckPointManagerV3::AddCheckPoint(const CheckPointPtr & checkPointPtr) {
    auto & fileKeyMap = mOffsetMap[checkPointPtr->mLogMode4Path];
    fileKeyMap[checkPointPtr->mFileKey] = checkPointPtr;
}

void CheckPointManagerV3::DeleteCheckPoint(DevInode devInode, const std::string& logMode4Path) {
    if (auto it = mOffsetMap.find(logMode4Path); it != mOffsetMap.end()) {
        auto & fileKeyMap = it->second;
        if (auto fileKeyIter = fileKeyMap.find(devInode.ToString()); fileKeyIter != fileKeyMap.end()) {
            fileKeyMap.erase(fileKeyIter);
        }
        if (fileKeyMap.empty()) {
            mOffsetMap.erase(it);
        }
    }
}

bool CheckPointManagerV3::GetCheckPoint(DevInode devInode, const std::string & logMode4Path, CheckPointPtr & checkPointPtr) {
    if (auto it = mOffsetMap.find(logMode4Path); it != mOffsetMap.end()) {
        auto & fileKeyMap = it->second;
        if (auto fileKeyIter = fileKeyMap.find(devInode.ToString()); fileKeyIter != fileKeyMap.end()) {
            checkPointPtr = fileKeyIter->second;
            return true;
        }
    }
    return false;
}

std::unordered_map<std::string, CheckPointPtr> & CheckPointManagerV3::GetCheckPoints(const std::string & logMode4Path) {
    return mOffsetMap[logMode4Path];
}

void CheckPointManagerV3::RemoveAllCheckPoint() {
    mOffsetMap.clear();
}

bool CheckPointManagerV3::NeedDump(int32_t curTime) {
    // random 60 second to protect burst
    // remember "+1", to avoid mod 0 which will cause SIGFPE
    return curTime - mLastDumpTime
        > rand() % (INT32_FLAG(dd_check_point_dump_interval) / 15 + 1) + INT32_FLAG(dd_check_point_dump_interval);
}

void CheckPointManagerV3::ResetLastDumpTime() {
    mLastDumpTime = (int32_t)time(NULL);
}

void CheckPointManagerV3::LoadCheckPoint() {
    const std::string & dirPath = AppConfig::GetInstance()->GetCheckPointFilePath();
    
    if (fs::exists(dirPath)) {
        LoadDirCheckPoint(dirPath);
    } else {
        std::string tmpDirPath = dirPath + TMP_SUFFIX;
        if (fs::exists(tmpDirPath)) {
            LoadDirCheckPoint(tmpDirPath);
            /// 将临时目录改成正式目录
            fs::rename(tmpDirPath, dirPath);
        }
    }
    LOG_INFO(sLogger,
             ("load checkpoint, version", mLoadVersion)("file check point", mOffsetMap.size()));
}

void CheckPointManagerV3::LoadDirCheckPoint(fs::path dirPath) {
    auto logMode4Paths = listChildren(dirPath);

    for (const auto & entry : fs::directory_iterator(dirPath)) {
        std::string logMode4Path = entry.path().filename().string();
        if (logMode4Path.find(DDCLOUD_ERROR_TARGET_TAG) != std::string::npos) {
            continue;
        }

        fs::path fileName = dirPath / logMode4Path;

        std::vector<std::string> content = readFileContent(fileName);
        if (content.empty()) {
            LOG_DEBUG(sLogger, ("read checkpoint file is empty, file", fileName));
            continue;
        }

        std::string configName;

        for (const auto & c : content) {
            auto splitedContents = StringSpliter(c, SEPARATORCHAR);
            if (splitedContents.size() != 2) {
                LOG_ERROR(sLogger, ("OffsetManager-load-error", "offset info error")("content", c));
                continue;
            }

            Json::Value jsonRoot;
            if (!parseJsonString(splitedContents[1], jsonRoot)) {
                LOG_ERROR(sLogger, ("parse json failed, logMode4Path", logMode4Path));
                continue;
            }

            int32_t logID = jsonRoot[LOGID_STR].asInt();
            int32_t pathID = jsonRoot[PATHID_STR].asInt();
            int64_t offSet = jsonRoot[OFFSET_STR].asInt64();
            // int64_t timeStamp = jsonRoot[TIMESTAMP_STR].asInt64();
            std::string parentPath = jsonRoot[PARENTPATH_STR].asString();
            std::string fileName = jsonRoot[FILENAME_STR].asString();
            int64_t lastModifyTime = jsonRoot[LAST_MODIFY_TIME_STR].asInt64();
            std::string fileKey = jsonRoot[FILE_KEY].asString();
            std::string fileHeadMd5 = jsonRoot[FILE_HEAD_MD5].asString();

            auto devAndInode = SplitString(fileKey, "_");
            if (devAndInode.size() != 2) {
                continue;
            }
            uint64_t dev = StringTo<uint64_t>(devAndInode[0]);
            uint64_t inode = StringTo<uint64_t>(devAndInode[1]);

            if (configName.empty()) {
                std::string pathIdStr = ToString(pathID);
                if (auto pos = logMode4Path.find(pathIdStr); pos != std::string::npos) {
                    configName = logMode4Path.substr(0, pos + pathIdStr.size());
                } else {
                    continue;
                }
            }

            CheckPointPtr checkPoint = CheckPointPtr(new CheckPoint(
                offSet, fileHeadMd5, {dev, inode}, logMode4Path, configName, fileKey, fs::path(parentPath) / fileName, lastModifyTime, logID, pathID));

            AddCheckPoint(checkPoint);
        }
    }
}

bool CheckPointManagerV3::DumpCheckPointToLocal() {
    try {
        ResetLastDumpTime();
        const std::string & dirPath = AppConfig::GetInstance()->GetCheckPointFilePath();
        fs::path tmpDirPath = dirPath + TMP_SUFFIX;

        fs::remove_all(tmpDirPath);
        fs::create_directory(tmpDirPath);

        bool isSuccess = true;
        time_t currentTime = time(nullptr);
        for (const auto & [logMode4PathKey, fileKeyMap] : mOffsetMap) {
            try {
                if (fileKeyMap.empty()) {
                    continue;
                }

                std::vector<std::string> res;
                for (const auto & [fileKey, checkPoint] : fileKeyMap) {
                    if (checkPoint->mLastUpdateTime != 0 && currentTime - checkPoint->mLastUpdateTime > _2WEEK) {
                        // 过滤掉2周之前的offset信息,防止offset暴增无法清理
                        continue;
                    }

                    Json::Value node;
                    node[LOGID_STR] = checkPoint->mLogID;
                    node[PATHID_STR] = checkPoint->mPathID;
                    node[OFFSET_STR] = checkPoint->mOffset;
                    node[TIMESTAMP_STR] = checkPoint->mLastUpdateTime;
                    node[PARENTPATH_STR] = fs::path(checkPoint->mRealFileName).parent_path().string();
                    node[FILENAME_STR] = fs::path(checkPoint->mRealFileName).filename().string();
                    node[FILE_KEY] = checkPoint->mFileKey;
                    node[LAST_MODIFY_TIME_STR] = checkPoint->mLastUpdateTime;
                    node[FILE_HEAD_MD5] = checkPoint->mFileHeadMd5;

                    std::string content;
                    content.append(ToString(checkPoint->mLogID));
                    content.append("_");
                    content.append(ToString(checkPoint->mPathID));
                    content.append("_");
                    content.append(ToString(checkPoint->mLastUpdateTime));
                    content.append(SEPARATORCHAR);

                    Json::StreamWriterBuilder writer;
                    writer["indentation"] = "";
                    content.append(Json::writeString(writer, node));
                    res.emplace_back(std::move(content));
                }

                fs::path filePath = tmpDirPath / logMode4PathKey;
                std::ofstream fout(filePath.c_str());
                if (!fout) {
                    isSuccess = false;
                    LOG_ERROR(sLogger, ("file is can not be created. file ", filePath));
                }

                for (const auto & content : res) {
                    fout <<  content;
                    fout << "\r\n";
                }
                if (!fout.good()) {
                    LOG_ERROR(sLogger, ("write file failed, file", filePath));
                    isSuccess = false;
                    fout.close();
                }
                fout.flush();
                fout.close();
            } catch (const std::exception & e) {
                isSuccess = false;
                LOG_ERROR(sLogger, ("dump check point faild, logMode4Pathkey", logMode4PathKey)("error", e.what()));
            }
        }

        if (isSuccess) {
            // 首先复制到del目录下，并保证del下的内容全部被删除(删除操作不是原子操作)，防止中途宕机，之后启动的时候加载老的offset
            if (fs::exists(dirPath)) {
                std::string del = dirPath + DEL_SUFFIX;
                fs::remove_all(dirPath + DEL_SUFFIX);
                fs::rename(dirPath, del);
                fs::remove_all(dirPath + DEL_SUFFIX);
            }
            fs::rename(tmpDirPath, dirPath);
            return true;
        }
    } catch(const std::exception & e) {
        LOG_ERROR(sLogger, ("dump check point to local, error", e.what()));
    }
    return false;
}

} // namespace logtail
