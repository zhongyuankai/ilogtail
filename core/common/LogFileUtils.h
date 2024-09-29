#include "models/StringView.h"

namespace logtail {

/** 截取行中时间字符串 */
StringView getTimeStringFromLineByIndex(const char* buffer, size_t size, const std::string & startFlag, int32_t startFlagIndex, int32_t timeFormatLength);

/** 解析时间，解析失败返回-1 */
time_t parseTime(const StringView & timeString, const std::string & timeFormat);

/** 转换Java风格的时间格式到C的strptime格式 */
std::string convertJavaFormatToStrptime(const std::string & javaFormat);

}
