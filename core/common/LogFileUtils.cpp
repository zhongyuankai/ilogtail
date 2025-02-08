#include "LogFileUtils.h"
#include "common/Strptime.h"
#include "common/MachineInfoUtil.h"

#include <vector>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <boost/lexical_cast.hpp>
#include "absl/time/time.h"
#include "absl/time/clock.h"
#include "openssl/md5.h"
#include "logger/Logger.h"

namespace fs = std::filesystem; 


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

int64_t parseTime(const StringView & timeString, const std::string & timeFormat) {
    try {
        if (timeFormat == "LongType-Time") {
            return boost::lexical_cast<int64_t>(timeString);
        }

        absl::string_view s(timeString.data(), timeString.size());
        absl::Time dateTime;
        std::string err;
        static absl::TimeZone localTz = absl::LocalTimeZone();
        if (absl::ParseTime(timeFormat, s, localTz, &dateTime, &err)) {
            return absl::ToUnixMillis(dateTime);
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
        return "%Y-%m-%dT%H:%M:%E3S";
    } else if (javaFormat == "yyyy/MM/dd HH:mm:ss") {
        return "%Y/%m/%d %H:%M:%S";
    } else if (javaFormat == "dd/MMM/yyyy:HH:mm:ss") {
        return "%d/%b/%Y:%H:%M:%S";
    }
    return "";
}

std::string getHostName() {
    static std::string HOSTNAME;
    if (HOSTNAME.empty()) {
        std::string hostname = GetHostName();
        std::string DIDIDOMAIN = ".diditaxi.com";
        if (hostname.find(DIDIDOMAIN) != std::string::npos) {
            HOSTNAME = hostname.substr(0, hostname.size() - DIDIDOMAIN.size());
        }
    }
    return HOSTNAME;
}

std::string getHostNameSuffix() {
    static std::string HOSTNAMESUFFIX;
    if (HOSTNAMESUFFIX.empty()) {
        std::string hostname = getHostName();
        if (hostname.find(".") != std::string::npos) {
            HOSTNAMESUFFIX = ".docker" + hostname.substr(hostname.find_last_of("."));
            std::cout << HOSTNAMESUFFIX << std::endl;
        }
    }
    return HOSTNAMESUFFIX;
}


std::string getMd5ByHostName(std::string plainText, std::string & hostname) {
    if (!hostname.empty()) {
        plainText += hostname + getHostNameSuffix();
    } else {
        plainText += getHostName();
    }

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(plainText.c_str()), plainText.size(), digest);

    /// 将哈希结果转换为十六进制字符串
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(digest[i]);
    }
    
    return oss.str();
}

constexpr const char* MD5_FAILED_TAG = "Failed";
constexpr size_t FILE_HEAD_LENGTH = 1024;

std::string getMd5(const std::string& content) {
    try {
        // 初始化 MD5 对象
        unsigned char digest[MD5_DIGEST_LENGTH];
        MD5(reinterpret_cast<const unsigned char*>(content.c_str()), 
            content.size() > FILE_HEAD_LENGTH ? FILE_HEAD_LENGTH : content.size(),
            digest);

        // 转为十六进制字符串
        const char * chars = "0123456789ABCDEF";
        std::ostringstream hexResult;
        for (unsigned char byte : digest) {
            hexResult << chars[(byte >> 4) & 0xF]; // 高四位
            hexResult << chars[byte & 0xF];       // 低四位
        }
        std::cout << std::endl;

        return hexResult.str();
    } catch (const std::exception& e) {
        return MD5_FAILED_TAG;
    }
}

bool isNotBlank(const std::string & str) {
    for (char c : str) {
        if (!isspace(static_cast<unsigned char>(c))) {
            return true;
        }
    }
    return false;
}

int StringToInt32(const std::string& str) {
    try {
        return StringTo<int32_t>(str);
    } catch (...) {
        return -1;
    }
}


std::string readFirstLine(LogFileOperator & fileOp) {
    char buffer[1024];
    size_t offset = 0;
    size_t nbytes;
    std::string cache; // 存储从上一个块遗留下的内容
    std::string line;
    while (nbytes = fileOp.Pread(buffer, 1, 1024, offset) > 0) {
        offset += nbytes;
        cache.append(buffer, nbytes);
        size_t pos;

        while (true) {
            pos = cache.find('\r');
            if (pos != std::string::npos) {
                line = cache.substr(0, pos);
                if (isNotBlank(line)) {
                    return line;
                }
                if (pos + 1 < cache.size() && cache[pos + 1] == '\n') {
                    cache.erase(0, pos + 2);
                } else {
                    cache.erase(0, pos + 1);
                }
            } else if (pos = cache.find('\n'); pos != std::string::npos) {
                line = cache.substr(0, pos);
                if (isNotBlank(line)) {
                    return line;
                }
                cache.erase(0, pos + 1);
            } else {
                break;
            }
        }
    }

    return isNotBlank(line) ? line : "";
}

}
