#include "LogFileUtils.h"
#include "common/Strptime.h"

#include <vector>

namespace logtail {

StringView getTimeStringFromLineByIndex(const char* buffer, size_t size, const std::string & startFlag, int32_t startFlagIndex, int32_t timeFormatLength) {
    if (startFlag.empty() && startFlagIndex == 0) {
        if (size < timeFormatLength) {
            return {};
        }
        
        return {StringView(buffer, timeFormatLength)};
    }
    
    if (!startFlag.empty()) {
        StringView line(buffer, size);
        bool isVaild = true;
        size_t currentIndex = 0;

        for (int32_t i = 0; i < startFlagIndex + 1; ++i) {
            size_t index = line.find(startFlag, currentIndex);
            if (index != std::string::npos) {
                currentIndex = index + startFlag.size();
            } else {
                /// 该中不存在符合条件的startFlag
                isVaild = false;
                break;
            }
        }

        if (!isVaild) {
            return {};
        }

        /// 在currentIndex后的字符串是否符合timeFormat的长度
        if (size - currentIndex < timeFormatLength) {
            return {};
        }

        return {buffer + currentIndex, timeFormatLength};
    }

    return {};
}

time_t parseTime(const StringView & timeString, const std::string & timeFormat) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (auto ret = strptime(timeString.data(), timeFormat.c_str(), &tm); ret != NULL) {
        tm.tm_isdst = -1;
        return mktime(&tm);
    }
    return -1;
}

std::string convertJavaFormatToStrptime(const std::string & javaFormat) {
    static const std::vector<std::pair<std::string, std::string>> formatMap = {
        {"yyyy", "%Y"},    // 四位数年份
        {"yy", "%y"},      // 两位数年份
        {"MM", "%m"},      // 月份（01-12）
        {"M", "%m"},       // 月份（1-12）
        {"dd", "%d"},      // 日期（01-31）
        {"d", "%d"},       // 日期（1-31）
        {"HH", "%H"},      // 24小时制小时（00-23）
        {"H", "%H"},       // 24小时制小时（0-23）
        {"hh", "%I"},      // 12小时制小时（01-12）
        {"h", "%I"},       // 12小时制小时（1-12）
        {"mm", "%M"},      // 分钟（00-59）
        {"ss", "%S"},      // 秒（00-59）
        {"SSS", "%f"},     // 毫秒（000-999）
        {"Z", "%z"},       // 时区（+hhmm或-hhmm）
        {"a", "%p"},       // AM/PM
    };

    std::string cFormat;
    size_t pos = 0;

    // 遍历 Java 格式字符串
    while (pos < javaFormat.size()) {
        bool matched = false;

        for (const auto & [java, c] : formatMap) {
            if (javaFormat.compare(pos, java.size(), java) == 0) {
                cFormat.append(c);
                pos += java.size();
                matched = true;
                break; // 找到后跳出循环，继续下一个字符
            }
        }

        // 处理未匹配的情况（添加原始字符）
        if (!matched) {
            cFormat.push_back(javaFormat[pos]);
            pos++;
        }
    }
    return cFormat;
}

}
