#include "LogstoreKafkaSenderQueue.h"
#include "common/Flags.h"
#include "common/StringTools.h"
#include "monitor/LogtailAlarm.h"


DEFINE_FLAG_INT32(max_kafka_send_error_count, "set disabled if last send is all failed", 60);
DEFINE_FLAG_INT32(kafka_disable_send_retry_interval, "disabled client can retry to send after seconds", 300);
DEFINE_FLAG_INT32(kafka_disable_send_retry_interval_max,
                  "max : disabled client can retry to send after seconds",
                  3600);
DEFINE_FLAG_DOUBLE(kafka_disable_send_retry_interval_scale, "", 1.5);
DEFINE_FLAG_INT32(max_kafka_quota_exceed_count, "set disabled if last send is all quota exceed", 1);
DEFINE_FLAG_INT32(kafka_quota_send_retry_interval, "quota exceed client can retry to send after seconds", 3);
DEFINE_FLAG_INT32(kafka_quota_send_retry_interval_max,
                  "max : quota exceed client can retry to send after seconds",
                  60);
DEFINE_FLAG_INT32(kafka_quota_send_concurrency_min, "quota exceed client's send concurrency after recover", 1);
DEFINE_FLAG_INT32(kafka_send_concurrency_trigger,
                  "trigger send if now consurrency <= this value (kafka_send_concurrency_max/2)",
                  256);
DEFINE_FLAG_INT32(kafka_send_concurrency_max, "max concurrency of one client", 512);
DEFINE_FLAG_INT32(kafka_send_concurrency_max_update_time, "max update time seconds", 300);
DEFINE_FLAG_DOUBLE(kafka_quota_send_retry_interval_scale, "", 2.0);

namespace logtail {

LogstoreKafkaSenderInfo::LogstoreKafkaSenderInfo()
    : mLastNetworkErrorCount(0),
      mLastQuotaExceedCount(0),
      mLastNetworkErrorTime(0),
      mLastQuotaExceedTime(0),
      mNetworkRetryInterval((double)INT32_FLAG(kafka_disable_send_retry_interval)),
      mQuotaRetryInterval((double)INT32_FLAG(kafka_quota_send_retry_interval)),
      mNetworkValidFlag(true),
      mQuotaValidFlag(true),
      mSendConcurrency(INT32_FLAG(kafka_send_concurrency_max)) {
    mSendConcurrencyUpdateTime = time(NULL);
}

bool LogstoreKafkaSenderInfo::CanSend(int32_t curTime) {
    if (!mNetworkValidFlag) {
        if (curTime - mLastNetworkErrorTime >= (int32_t)mNetworkRetryInterval) {
            LOG_INFO(sLogger,
                     ("Network fail timeout,  try send data again",
                      this->mRegion)("last error", mLastNetworkErrorTime)("retry interval", mNetworkRetryInterval));
            mNetworkRetryInterval *= DOUBLE_FLAG(kafka_disable_send_retry_interval_scale);
            if (mNetworkRetryInterval > (double)INT32_FLAG(kafka_disable_send_retry_interval_max)) {
                mNetworkRetryInterval = (double)INT32_FLAG(kafka_disable_send_retry_interval_max);
            }

            // set lastErrorTime to curTime, make sure client can only try once between
            // kafka_disable_send_retry_interval seconds
            mLastNetworkErrorTime = curTime;
            // can try INT32_FLAG(max_kafka_send_error_count) times
            mLastNetworkErrorCount = 0;
            mNetworkValidFlag = true;
        } else {
            return false;
        }
    } else if (!mQuotaValidFlag) {
        if (curTime - mLastQuotaExceedTime >= (int32_t)mQuotaRetryInterval) {
            LOG_INFO(sLogger,
                     ("Quota fail timeout,  try send data again",
                      this->mRegion)("last error", mLastQuotaExceedTime)("retry interval", mQuotaRetryInterval));
            mQuotaRetryInterval *= DOUBLE_FLAG(kafka_quota_send_retry_interval_scale);
            if (mQuotaRetryInterval > (double)INT32_FLAG(kafka_quota_send_retry_interval_max)) {
                mQuotaRetryInterval = (double)INT32_FLAG(kafka_quota_send_retry_interval_max);
            }

            mLastQuotaExceedTime = curTime;
            mLastQuotaExceedCount = 0;
            mQuotaValidFlag = true;
        } else {
            return false;
        }
    }
    return true;
}

bool LogstoreKafkaSenderInfo::RecordSendResult(SendResult rst, LogstoreKafkaSenderStatistics& statisticsItem) {
    switch (rst) {
        case LogstoreKafkaSenderInfo::SendResult_OK:
            ++statisticsItem.mSendSuccessCount;
            mLastNetworkErrorCount = 0;
            mLastQuotaExceedCount = 0;
            mNetworkValidFlag = true;
            mQuotaValidFlag = true;
            mNetworkRetryInterval = (double)INT32_FLAG(kafka_disable_send_retry_interval);
            mQuotaRetryInterval = (double)INT32_FLAG(kafka_quota_send_retry_interval) + rand() % 5;
            mSendConcurrency += 2;
            if (mSendConcurrency > INT32_FLAG(kafka_send_concurrency_max)) {
                mSendConcurrency = INT32_FLAG(kafka_send_concurrency_max);
            } else {
                mSendConcurrencyUpdateTime = time(NULL);
            }
            // return true only if concurrency < kafka_send_concurrency_trigger
            return mSendConcurrency < INT32_FLAG(kafka_send_concurrency_trigger);
            break;
        case LogstoreKafkaSenderInfo::SendResult_DiscardFail:
            ++statisticsItem.mSendDiscardErrorCount;
            mLastNetworkErrorCount = 0;
            mLastQuotaExceedCount = 0;
            mNetworkValidFlag = true;
            mQuotaValidFlag = true;
            mNetworkRetryInterval = (double)INT32_FLAG(kafka_disable_send_retry_interval);
            mQuotaRetryInterval = (double)INT32_FLAG(kafka_quota_send_retry_interval) + rand() % 5;
            // only if send success or discard, mSendConcurrency inc 2
            mSendConcurrency += 2;
            if (mSendConcurrency > INT32_FLAG(kafka_send_concurrency_max)) {
                mSendConcurrency = INT32_FLAG(kafka_send_concurrency_max);
            } else {
                mSendConcurrencyUpdateTime = time(NULL);
            }
            // return true only if concurrency < kafka_send_concurrency_trigger
            return mSendConcurrency < INT32_FLAG(kafka_send_concurrency_trigger);
            break;
        case LogstoreKafkaSenderInfo::SendResult_NetworkFail:
            ++statisticsItem.mSendNetWorkErrorCount;
            mLastNetworkErrorTime = time(NULL);
            // only if send success or discard, mSendConcurrency inc 2
            ++mSendConcurrency;
            if (mSendConcurrency > INT32_FLAG(kafka_send_concurrency_max)) {
                mSendConcurrency = INT32_FLAG(kafka_send_concurrency_max);
            } else {
                mSendConcurrencyUpdateTime = mLastNetworkErrorTime;
            }
            if (++mLastNetworkErrorCount >= INT32_FLAG(max_kafka_send_error_count)) {
                mLastNetworkErrorCount = INT32_FLAG(max_kafka_send_error_count);
                mNetworkValidFlag = false;
            }
            break;
        case LogstoreKafkaSenderInfo::SendResult_QuotaFail:
            ++statisticsItem.mSendQuotaErrorCount;
            mLastQuotaExceedTime = time(NULL);
            // if quota fail, set mSendConcurrency to min
            mSendConcurrency = INT32_FLAG(kafka_quota_send_concurrency_min);
            mSendConcurrencyUpdateTime = mLastQuotaExceedTime;
            if (++mLastQuotaExceedCount >= INT32_FLAG(max_kafka_quota_exceed_count)) {
                mLastQuotaExceedCount = INT32_FLAG(max_kafka_quota_exceed_count);
                mQuotaValidFlag = false;
            }
            break;
        default:
            // only if send success or discard, mSendConcurrency inc 2
            ++mSendConcurrency;
            if (mSendConcurrency > INT32_FLAG(kafka_send_concurrency_max)) {
                mSendConcurrency = INT32_FLAG(kafka_send_concurrency_max);
            } else {
                mSendConcurrencyUpdateTime = time(NULL);
            }
            break;
    }
    return false;
}

bool LogstoreKafkaSenderInfo::OnRegionRecover(const std::string& region) {
    if (region == mRegion) {
        bool rst = !(mNetworkValidFlag && mQuotaValidFlag);
        LogstoreKafkaSenderStatistics senderStatistics;
        RecordSendResult(LogstoreKafkaSenderInfo::SendResult_OK, senderStatistics);
        LOG_DEBUG(sLogger, ("On region recover  ", this->mRegion));
        return rst;
    }
    return false;
}


void LogstoreKafkaSenderInfo::SetRegion(const std::string& region) {
    if (!mRegion.empty()) {
        return;
    }
    mRegion = region;
}

bool LogstoreKafkaSenderInfo::ConcurrencyValid() {
    if (mSendConcurrency <= 0) {
        // check consurrency update time
        int32_t nowTime = time(NULL);
        if (nowTime - mSendConcurrencyUpdateTime > INT32_FLAG(kafka_send_concurrency_max_update_time)) {
            mSendConcurrency = INT32_FLAG(kafka_quota_send_concurrency_min);
            mSendConcurrencyUpdateTime = time(NULL);
        }
    }
    return mSendConcurrency > 0;
}

LogstoreKafkaSenderStatistics::LogstoreKafkaSenderStatistics() {
    Reset();
}

void LogstoreKafkaSenderStatistics::Reset() {
    mMaxUnsendTime = 0;
    mMinUnsendTime = 0;
    mMaxSendSuccessTime = 0;
    mSendQueueSize = 0;
    mSendNetWorkErrorCount = 0;
    mSendQuotaErrorCount = 0;
    mSendDiscardErrorCount = 0;
    mSendSuccessCount = 0;
    mSendBlockFlag = false;
    mValidToSendFlag = false;
}

} // namespace logtail
