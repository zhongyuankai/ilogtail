// Copyright 2023 iLogtail Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pipeline/PipelineContext.h"

#include "flusher/FlusherSLS.h"
#include "flusher/FlusherKafka.h"
#include "common/StringTools.h"
#include "common/LogFileUtils.h"

using namespace std;

namespace logtail {

const string PipelineContext::sEmptyString = "";

const string& PipelineContext::GetProjectName() const {
    return mSLSInfo ? mSLSInfo->mProject : sEmptyString;
}

const string& PipelineContext::GetLogstoreName() const {
    return mSLSInfo ? mSLSInfo->mLogstore : sEmptyString;
}

const string& PipelineContext::GetRegion() const {
    return mSLSInfo ? mSLSInfo->mRegion : sEmptyString;
}

LogstoreFeedBackKey PipelineContext::GetLogstoreKey() const {
    if (mKey != -1) {
        return mKey;
    }
    if (mSLSInfo) {
        return mSLSInfo->GetLogstoreKey();
    } else {
        static LogstoreFeedBackKey key = GenerateLogstoreFeedBackKey(sEmptyString, sEmptyString);
        return key;
    }
}

void PipelineContext::SetFlusherKafka(const FlusherKafka * flusherKafka) {
    mFlusherKafka = flusherKafka; 
    mKey = flusherKafka->GetLogstoreKey();
}

void PipelineContext::SetLogMod4PathKey(const std::string & path, bool isCollectContainer) {
    /// [hostname_]logId_pathId
    auto list = SplitString(mConfigName, "_");
    if (isCollectContainer && list.size() == 3) {
        mLogId = StringToInt32(list[1]);
        mPathId = StringToInt32(list[2]);
    } else if (!isCollectContainer && list.size() == 2) {
        mLogId = StringToInt32(list[0]);
        mPathId = StringToInt32(list[1]);
    } else {
        mLogId = -1;
        mPathId = -1;
    }

    if (!path.empty()) {
        std::string logMod4PathKey = TrimString(path);
        if (EndWith(logMod4PathKey, "*")) {
            logMod4PathKey = logMod4PathKey.substr(0, logMod4PathKey.size() - 1);
        }

        if (logMod4PathKey.find("/") != std::string::npos) {
            ReplaceString(logMod4PathKey, "/", "_");
        }

        mLogMod4PathKey = mConfigName + "_" + logMod4PathKey;
    } else {
        mLogMod4PathKey = mConfigName;
    }

    /// 兼容offset文件过长的问题
    if (isCollectContainer && mLogMod4PathKey.size() > 250 && list.size() == 3) {
        mLogMod4PathKey = mConfigName + "_" + getMd5ByHostName(path, list[0]);
    }
}


} // namespace logtail
