#include "models/StringView.h"
#include "common/LogFileOperator.h"
#include "common/StringTools.h"

namespace logtail {

constexpr const char * MD5_FAILED_TAG = "Failed";
constexpr size_t FILE_HEAD_LENGTH = 1024;

/** 截取行中时间字符串 */
StringView getTimeStringFromLineByIndex(const char* buffer, size_t size, const std::string & startFlag, int32_t startFlagIndex, int32_t timeFormatLength);

/** 解析时间，解析失败返回-1 */
int64_t parseTime(const StringView & timeString, const std::string & timeFormat);

/** 转换Java风格的时间格式到C的strptime格式 */
std::string convertJavaFormatToStrptime(const std::string & javaFormat);

/** 兼容SWAN */
std::string getMd5(const std::string& content);
/** 根据plainText获取其对应的32位md5编码 */
std::string getMd5ByHostName(std::string plainText, std::string & hostname);
std::string getHostName();
std::string getHostNameSuffix();
std::string readFirstLine(LogFileOperator & fileOp);
bool isNotBlank(const std::string & str);

int StringToInt32(const std::string& str);


}
