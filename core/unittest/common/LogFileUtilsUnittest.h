#include "unittest/Unittest.h"
#include "common/LogFileUtils.h"
#include "common/HashUtil.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace logtail {

class LogFileUtilsUnittest : public ::testing::Test {
public:
    void TestGetTimeStringFromLineByIndex();

    void TestParseTime();

    void TestGetFileNodeHeadMd5();
    void TestGetMd5();
};

APSARA_UNIT_TEST_CASE(LogFileUtilsUnittest, TestGetTimeStringFromLineByIndex, 0);
APSARA_UNIT_TEST_CASE(LogFileUtilsUnittest, TestParseTime, 0);
APSARA_UNIT_TEST_CASE(LogFileUtilsUnittest, TestGetFileNodeHeadMd5, 0);
APSARA_UNIT_TEST_CASE(LogFileUtilsUnittest, TestGetMd5, 0);

void LogFileUtilsUnittest::TestGetTimeStringFromLineByIndex() {
    int32_t startFlagIndex = 1;
    std::string startFlag = "][";
    int32_t timeFormatLength = 19;
    std::string timeFormat = "yyyy-MM-dd HH:mm:ss";
    std::string s = "[Error][../file.h][2024-09-26 15:04:11]_undef||traceid=xxx||spanid=xxx||hintCode=0||_msg=redis setex";

    StringView timeString = getTimeStringFromLineByIndex(s.data(), s.size(), startFlag, startFlagIndex, timeFormatLength);
    APSARA_TEST_EQUAL("2024-09-26 15:04:11", timeString.to_string());

    std::string format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y-%m-%d %H:%M:%S", format);
    time_t timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727363051, timestamp);

    startFlagIndex = 0;
    startFlag = "[";
    timeFormatLength = 19;
    timeFormat = "yyyy-MM-dd HH:mm:ss";
    s = "[2024-09-26 15:04:11][../file.h][Error]_undef||traceid=xxx||spanid=xxx||hintCode=0||_msg=redis setex";

    timeString = getTimeStringFromLineByIndex(s.data(), s.size(), startFlag, startFlagIndex, timeFormatLength);
    APSARA_TEST_EQUAL("2024-09-26 15:04:11", timeString.to_string());

    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y-%m-%d %H:%M:%S", format);
    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727363051, timestamp);

    startFlagIndex = 0;
    startFlag = "";
    timeFormatLength = 19;
    timeFormat = "yyyy-MM-dd HH:mm:ss";
    s = "2024-09-26 15:04:11][../file.h][Error]_undef||traceid=xxx||spanid=xxx||hintCode=0||_msg=redis setex";

    timeString = getTimeStringFromLineByIndex(s.data(), s.size(), startFlag, startFlagIndex, timeFormatLength);
    APSARA_TEST_EQUAL("2024-09-26 15:04:11", timeString.to_string());

    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y-%m-%d %H:%M:%S", format);
    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727363051, timestamp);

    startFlagIndex = 100;
    startFlag = "][";
    timeFormatLength = 19;
    timeFormat = "yyyy-MM-dd HH:mm:ss";
    s = "2024-09-26 15:04:11][../file.h][Error]_undef||traceid=xxx||spanid=xxx||hintCode=0||_msg=redis setex";
    timeString = getTimeStringFromLineByIndex(s.data(), s.size(), startFlag, startFlagIndex, timeFormatLength);
    APSARA_TEST_EQUAL("", timeString.to_string());

    startFlagIndex = 0;
    startFlag = "][";
    timeFormatLength = 19;
    timeFormat = "yyyy-MM-dd HH:mm:ss";
    s = "2024-09-26 15:04:11][../file.h";
    timeString = getTimeStringFromLineByIndex(s.data(), s.size(), startFlag, startFlagIndex, timeFormatLength);
    APSARA_TEST_EQUAL("", timeString.to_string());
}

void LogFileUtilsUnittest::TestParseTime() {
    std::string timeFormat = "LongType-Time";
    std::string timeString = "1727191845000";
    std::string format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("LongType-Time", format);

    time_t timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727191845, timestamp);

    timeFormat = "yyyy-MM-dd HH:mm:ss";
    timeString = "2024-09-24 15:30:45";
    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y-%m-%d %H:%M:%S", format);

    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727191845, timestamp);

    timeFormat = "yyyy-MM-dd'T'HH:mm:ss";
    timeString = "2024-09-24T15:30:45";
    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y-%m-%dT%H:%M:%S", format);

    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727191845, timestamp);

    timeFormat = "yyyy-MM-dd'T'HH:mm:ss.SSS";
    timeString = "2024-09-24T15:30:45";
    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y-%m-%dT%H:%M:%S", format);

    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727191845, timestamp);

    timeFormat = "yyyy/MM/dd HH:mm:ss";
    timeString = "2024/09/24 15:30:45";
    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%Y/%m/%d %H:%M:%S", format);

    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727191845, timestamp);

    timeFormat = "dd/MMM/yyyy:HH:mm:ss";
    timeString = "24/Sep/2024:15:30:45";
    format = convertJavaFormatToStrptime(timeFormat);
    APSARA_TEST_EQUAL("%d/%b/%Y:%H:%M:%S", format);

    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1727191845, timestamp);

    timeString = "2024-12-24 15:49:11";
    timeFormat = "yyyy-MM-dd HH:mm:ss";
    format = convertJavaFormatToStrptime(timeFormat);
    timestamp = parseTime(timeString, format);
    APSARA_TEST_EQUAL(1735055351, timestamp);

    std::string s = "2024-12-24 15:49:11.308010||level=INFO||file=..xiaojukeji.com/gulfstream/price-api/models/bill.SetEstimateCache/cache.go:198 _undef||traceid=0748593b61cafc3f24a44f2d103a0403||spanid=384637183434218699";
    StringView times{s.data(), 19};
    timestamp = parseTime(times, format);
    APSARA_TEST_EQUAL(1735055351, timestamp);

}

void LogFileUtilsUnittest::TestGetFileNodeHeadMd5() {
    std::string path = "Md5test.log";
    std::vector<std::string> contents {
        "\n",
        "   ",
        "pera_stat_key=g_u_user_trans_suc_log",
        "asdff"
    };

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    for (const auto & content : contents) {
        file << content << "\r\n";
    }
    file.flush();
    file.close();

    std::string targetMd5 = "36586F1A60124E2D361A869FDBD76153";
    LogFileOperator fileOp;
    fileOp.Open(path.c_str(), false);
    for (int i = 0; i < 10; i++) {
        std::string firstLine = readFirstLine(fileOp);
        std::string md5 = targetMd5;
        CheckAndUpdateSignatureV3(firstLine, md5);
        APSARA_TEST_EQUAL(targetMd5, md5);
    }

    fs::remove(path);

    /// 测试空文件
    std::string path1 = "Md5test1.log";
    std::ofstream file1(path, std::ios::out | std::ios::trunc);
    file1.close();

    targetMd5 = "Failed";
    LogFileOperator fileOp1;
    fileOp1.Open(path1.c_str(), false);
    for (int i = 0; i < 10; i++) {
        std::string firstLine = readFirstLine(fileOp1);
        std::string md5 = targetMd5;
        CheckAndUpdateSignatureV3(firstLine, md5);
        APSARA_TEST_EQUAL(targetMd5, md5);
    }
    fs::remove(path1);
}

void LogFileUtilsUnittest::TestGetMd5() {
    std::string content = "timeStamp";
    std::string targetMd5 = "87A3CB5C3554B2D9D8A1A773AD0936BE";
    for (int i = 0; i < 10; i++) {
        std::string md5 = getMd5(content);
        APSARA_TEST_EQUAL(targetMd5, md5);
    }

    targetMd5 = "41dbc482bf74c306b7cf2a6d7a951bf5";
    std::string plainText = "hello world";
    std::string hostname = "ubuntu";
    std::string md5 = getMd5ByHostName(plainText, hostname);
    APSARA_TEST_EQUAL(targetMd5, md5);
}

}
