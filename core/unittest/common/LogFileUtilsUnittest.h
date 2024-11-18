#include "unittest/Unittest.h"
#include "common/LogFileUtils.h"

namespace logtail {

class LogFileUtilsUnittest : public ::testing::Test {
public:
    void TestGetTimeStringFromLineByIndex();

    void TestParseTime();
};

APSARA_UNIT_TEST_CASE(LogFileUtilsUnittest, TestGetTimeStringFromLineByIndex, 0);
APSARA_UNIT_TEST_CASE(LogFileUtilsUnittest, TestParseTime, 0);

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
}

}
