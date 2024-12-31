// Copyright 2023 iLogtail Authors
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

#include "pipeline/Pipeline.h"
#include "pipeline/PipelineManager.h"
#include "unittest/Unittest.h"

using namespace std;

namespace logtail {

class PipelineManagerUnittest : public testing::Test {
public:
    void TestPipelineManagement() const;
    void TestPipelineContent() const;
};

void PipelineManagerUnittest::TestPipelineManagement() const {
    PipelineManager::GetInstance()->mPipelineNameEntityMap["test1"] = make_shared<Pipeline>();
    PipelineManager::GetInstance()->mPipelineNameEntityMap["test2"] = make_shared<Pipeline>();

    APSARA_TEST_EQUAL(2, PipelineManager::GetInstance()->GetAllPipelineNames().size());
    APSARA_TEST_NOT_EQUAL(nullptr, PipelineManager::GetInstance()->FindPipelineByName("test1"));
    APSARA_TEST_EQUAL(nullptr, PipelineManager::GetInstance()->FindPipelineByName("test3"));
}

void PipelineManagerUnittest::TestPipelineContent() const {
    PipelineContext context;
    context.SetConfigName("107092_212418");
    context.SetLogMod4PathKey("/home/ubuntu/ilogtail/logtail_test.log*", false);

    APSARA_TEST_EQUAL("107092_212418__home_ubuntu_ilogtail_logtail_test.log", context.GetLogMod4PathKey());
    APSARA_TEST_EQUAL(107092, context.GetLogId());
    APSARA_TEST_EQUAL(212418, context.GetPathId());

    context.SetConfigName("ubuntu_107092_212418");
    context.SetLogMod4PathKey("/home/ubuntu/ilogtail/logtail_test.log*", true);

    APSARA_TEST_EQUAL("ubuntu_107092_212418__home_ubuntu_ilogtail_logtail_test.log", context.GetLogMod4PathKey());
    APSARA_TEST_EQUAL(107092, context.GetLogId());
    APSARA_TEST_EQUAL(212418, context.GetPathId());
}

UNIT_TEST_CASE(PipelineManagerUnittest, TestPipelineManagement)
UNIT_TEST_CASE(PipelineManagerUnittest, TestPipelineContent)

} // namespace logtail

UNIT_TEST_MAIN
