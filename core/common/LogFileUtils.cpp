#include "LogFileUtils.h"
#include "common/Strptime.h"

#include <vector>
#include <iostream>
#include <boost/lexical_cast.hpp>
#include "absl/time/time.h"
#include "absl/time/clock.h"


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
    try {
        if (timeFormat == "LongType-Time") {
            long long millis = boost::lexical_cast<long long>(timeString);
            return static_cast<time_t>(millis / 1000);
        }

        absl::string_view s(timeString.data(), timeString.size());
        absl::Time time;
        static thread_local std::string err;
        static absl::TimeZone local_tz = absl::LocalTimeZone();
        if (absl::ParseTime(timeFormat, s, local_tz, &time, &err)) {
            return absl::ToUnixSeconds(time);
        }
    } catch (...) {
        return -1;
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
