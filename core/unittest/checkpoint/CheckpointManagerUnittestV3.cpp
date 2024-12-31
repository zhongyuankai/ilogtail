// Copyright 2022 iLogtail Authors
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

#include "unittest/Unittest.h"
#include "checkpoint/CheckPointManagerV3.h"
#include "checkpoint/CheckPoint.h"
// #include "common/FileSystemUtil.h"
#include "common/Flags.h"
#include "app_config/AppConfig.h"

DECLARE_FLAG_INT32(checkpoint_find_max_file_count);

namespace logtail {

std::string kTestRootDir;

class CheckpointManagerV3Unittest : public ::testing::Test {
public:
    static void SetUpTestCase() {
        kTestRootDir = (bfs::path(GetProcessExecutionDir()) / ".swan-logOffSet").string();
        bfs::remove_all(kTestRootDir);
        bfs::create_directories(kTestRootDir);
        AppConfig::GetInstance()->SetLogtailSysConfDir(kTestRootDir);
        AppConfig::GetInstance()->SetCheckPointFilePath(kTestRootDir);
    }

    static void TearDownTestCase() { bfs::remove_all(kTestRootDir); }

    void TestLoadAndDump();
};

UNIT_TEST_CASE(CheckpointManagerV3Unittest, TestLoadAndDump);
void CheckpointManagerV3Unittest::TestLoadAndDump() {
    CheckPointManagerV3 * checkpointManager = CheckPointManagerV3::Instance();
    std::string logMode4Path = "107092_212418__data0_temporary_logs_ilogtail_test_log_ilogtail_test_16.log";
    CheckPointPtr checkpointPtr = CheckPointPtr(new CheckPoint(
        50479424, "878E891C43F4C57975F4DB43E3B6297F", {2065, 268436008}, logMode4Path,
        "107092_212418", "2065_268436008", "/data0/temporary/logs/ilogtail_test_log/ilogtail_test_16.log", time(nullptr), 107092, 212418
    ));
    checkpointManager->AddCheckPoint(checkpointPtr);

    CheckPointPtr checkpoint2Ptr = CheckPointPtr(new CheckPoint(
        50479423, "878E891C43F4C57975F4DB43E3B62973", {2065, 268433111}, logMode4Path,
        "107092_212418", "2065_268433111", "/data0/temporary/logs/ilogtail_test_log/ilogtail_test_16.log.1", time(nullptr), 107092, 212418
    ));
    checkpointManager->AddCheckPoint(checkpoint2Ptr);

    CheckPointPtr targeCheckPoint2Ptr;
    checkpointManager->GetCheckPoint({2065, 268433111}, logMode4Path, targeCheckPoint2Ptr);
    APSARA_TEST_EQUAL("2065_268433111", targeCheckPoint2Ptr->mFileKey);

    checkpointManager->DumpCheckPointToLocal();

    checkpointManager->RemoveAllCheckPoint();

    checkpointManager->LoadCheckPoint();

    CheckPointPtr targeCheckPoint3Ptr;
    checkpointManager->GetCheckPoint({2065, 268436008}, logMode4Path, targeCheckPoint3Ptr);
    APSARA_TEST_EQUAL(50479424, targeCheckPoint3Ptr->mOffset);
    APSARA_TEST_EQUAL("878E891C43F4C57975F4DB43E3B6297F", targeCheckPoint3Ptr->mFileHeadMd5);
    APSARA_TEST_EQUAL("/data0/temporary/logs/ilogtail_test_log/ilogtail_test_16.log", targeCheckPoint3Ptr->mRealFileName);
    CheckPointPtr targeCheckPoint4Ptr;
    checkpointManager->GetCheckPoint({2065, 268433111}, logMode4Path, targeCheckPoint4Ptr);
    APSARA_TEST_EQUAL(50479423, targeCheckPoint4Ptr->mOffset);
    APSARA_TEST_EQUAL("878E891C43F4C57975F4DB43E3B62973", targeCheckPoint4Ptr->mFileHeadMd5);
    APSARA_TEST_EQUAL("/data0/temporary/logs/ilogtail_test_log/ilogtail_test_16.log.1", targeCheckPoint4Ptr->mRealFileName);

    checkpointManager->RemoveAllCheckPoint();
    bfs::remove_all(kTestRootDir);
    bfs::create_directories(kTestRootDir);

    std::string filePath = (bfs::path(kTestRootDir) / "107090_212432__data0_temporary_logs_ilogtail_test_log_ilogtail_test_23.log").string();
    std::ofstream file(filePath, std::ios::out | std::ios::trunc);
    std::string content = "107090_212432_1733797133831###{\"timeStamp\":1733796831000,\"fileHeadMd5\":\"B85132AC3C6832A6F5865E1F5FE62569\",\"fileName\":\"ilogtail_test_23.log\",\"offSet\":283728992,\"parentPath\":\"/data0/temporary/logs/ilogtail_test_log\",\"lastModifyTime\":1733797133831,\"logId\":107090,\"fileKey\":\"2065_268436019\",\"pathId\":212432}";
    file << content;
    file.flush();
    file.close();

    checkpointManager->LoadCheckPoint();

    CheckPointPtr targeCheckPoint5Ptr;
    checkpointManager->GetCheckPoint({2065, 268436019}, "107090_212432__data0_temporary_logs_ilogtail_test_log_ilogtail_test_23.log", targeCheckPoint5Ptr);
    APSARA_TEST_EQUAL(283728992, targeCheckPoint5Ptr->mOffset);
    APSARA_TEST_EQUAL("B85132AC3C6832A6F5865E1F5FE62569", targeCheckPoint5Ptr->mFileHeadMd5);
    APSARA_TEST_EQUAL("/data0/temporary/logs/ilogtail_test_log/ilogtail_test_23.log", targeCheckPoint5Ptr->mRealFileName);

    checkpointManager->DeleteCheckPoint({2065, 268436019}, "107090_212432__data0_temporary_logs_ilogtail_test_log_ilogtail_test_23.log");
    CheckPointPtr targeCheckPoint6Ptr;
    checkpointManager->GetCheckPoint({2065, 268436019}, "107090_212432__data0_temporary_logs_ilogtail_test_log_ilogtail_test_23.log", targeCheckPoint6Ptr);
    APSARA_TEST_EQUAL(nullptr, targeCheckPoint6Ptr);
}

} // namespace logtail

UNIT_TEST_MAIN
