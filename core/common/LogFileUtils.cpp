#include "LogFileUtils.h"
#include "common/Strptime.h"

#include <vector>
#include <iostream>
#include <boost/lexical_cast.hpp>

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
    if (timeFormat == "LongType-Time") {
        try {
            long long millis = boost::lexical_cast<long long>(timeString);
            return static_cast<time_t>(millis / 1000);
        } catch (const boost::bad_lexical_cast & e) {
            return -1;
        }
    }

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (auto ret = strptime(timeString.data(), timeFormat.c_str(), &tm); ret != NULL) {
        tm.tm_isdst = -1;
        return mktime(&tm);
    }
    return -1;
}

std::string convertJavaFormatToStrptime(const std::string & javaFormat) {
    if (javaFormat == "NoLogTime") {
        return "NoLogTime";
    } else if (javaFormat == "LongType-Time") {
        return "LongType-Time";
    } else if (javaFormat == "yyyy-MM-dd HH:mm:ss") {
        return "%Y-%m-%d %H:%M:%S";
    } else if (javaFormat == "yyyy-MM-dd'T'HH:mm:ss") {
        return "%Y-%m-%dT%H:%M:%S";
    } else if (javaFormat == "yyyy-MM-dd'T'HH:mm:ss.SSS") {
        return "%Y-%m-%dT%H:%M:%S";
    } else if (javaFormat == "yyyy/MM/dd HH:mm:ss") {
        return "%Y/%m/%d %H:%M:%S";
    } else if (javaFormat == "dd/MMM/yyyy:HH:mm:ss") {
        return "%d/%b/%Y:%H:%M:%S";
    }
    return "";
}

}
