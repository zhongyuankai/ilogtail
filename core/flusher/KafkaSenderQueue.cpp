#include "flusher/KafkaSenderQueue.h"
#include <memory>

#include "Logger.h"
#include "common/Flags.h"


DEFINE_FLAG_INT32(max_send_failed_sleep_time, "seconds", 120);
DEFINE_FLAG_INT32(kafka_send_concurrency_max, "max concurrency of one client", 512);

namespace logtail {

KafkaSendInfo::KafkaSendInfo()
    : mLastSendErrorCount(0)
    , mLastSendErrorTime(0)
    , mSendConcurrency(INT32_FLAG(kafka_send_concurrency_max))
    , mSendValidFlag(true) {
}

bool KafkaSendInfo::CanSend(int32_t curTime) {
    if (mSendValidFlag) {
        return true;
    }

    /// 通过发送的错误数及时间来判断是否可以发送
    int32_t sleep_time = INT32_FLAG(max_send_failed_sleep_time);
    if (mLastSendErrorCount <= 10) {
        sleep_time = 10 + (1 << mLastSendErrorCount);
    }
    if (curTime - mLastSendErrorTime < sleep_time) {
        return false;
    }

    return true;
}

void KafkaSendInfo::RecordSendResult(SendResult rst, KafkaSendStatistics & statisticsItem) {
    switch (rst) {
        case KafkaSendInfo::SendResult_OK:
            ++statisticsItem.mSendSuccessCount;
            statisticsItem.mMaxSendSuccessTime = time(NULL);
            mLastSendErrorCount = 0;
            mSendValidFlag = true;
            
            mSendConcurrency *= 2;
            if (mSendConcurrency > INT32_FLAG(kafka_send_concurrency_max)) {
                mSendConcurrency = INT32_FLAG(kafka_send_concurrency_max);
            }
            break;
        case KafkaSendInfo::SendResult_SendFail:
        case KafkaSendInfo::SendResult_OtherFail:
            ++statisticsItem.mSendErrorCount;
            mLastSendErrorCount++;
            mLastSendErrorTime = time(NULL);
            mSendConcurrency = 1;
            mSendValidFlag = false;
            break;
        default:
           break;
    }
}

void KafkaSendStatistics::Reset() {
    mMaxUnsendTime = 0;
    mMinUnsendTime = 0;
    mMaxSendSuccessTime = 0;
    mSendQueueSize = 0;

    mSendSuccessCount = 0;
    mSendErrorCount = 0;
    mSendBlockFlag = false;
    mValidToSendFlag = false;
}

template <class PARAM>
bool KafkaSendQueue<PARAM>::IsValid() const {
    if (this->IsEmpty()) {
        return true;
    }
    if (!mSendInfo.mSendValidFlag) {
        return false;
    }

    return this->mValid;
}

template <class PARAM>
void KafkaSendQueue<PARAM>::GetAllLoggroupWithLimit(std::vector<LoggroupEntry *> & logGroupVec) {
    if (this->mSize == 0) {
        return;
    }

    uint64_t index = this->mRead;
    const uint64_t endIndex = this->mWrite;
    for (int size = 0; index < endIndex; ++index) {
        LoggroupEntry* item = this->mArray[index % this->SIZE];
        if (item == NULL || item->mStatus != LoggroupKafkaSendStatus_Idle) {
            continue;
        }
        item->mStatus = LoggroupKafkaSendStatus_Sending;
        logGroupVec.push_back(item);
        if (++size >= mSendInfo.mSendConcurrency) {
            return;
        }

    }
}

template <class PARAM>
void KafkaSendQueue<PARAM>::GetAllLoggroup(std::vector<LoggroupEntry *> & logGroupVec) {
    if (this->mSize == 0) {
        return;
    }

    uint64_t index = this->mRead;
    const uint64_t endIndex = this->mWrite;
    for (; index < endIndex; ++index) {
        LoggroupEntry * item = this->mArray[index % this->SIZE];
        if (item == NULL || item->mStatus != LoggroupKafkaSendStatus_Idle) {
            continue;
        }
        item->mStatus = LoggroupKafkaSendStatus_Sending;
        logGroupVec.push_back(item);
    }
}

// with empty item
template <class PARAM>
bool KafkaSendQueue<PARAM>::InsertItem(LoggroupEntry* item) {
    if (this->IsFull()) {
        return false;
    }
    auto index = this->mRead;
    for (; index < this->mWrite; ++index) {
        if (this->mArray[index % this->SIZE] == NULL) {
            break;
        }
    }
    this->mArray[index % this->SIZE] = item;
    item->mIndex = index;
    if (index == this->mWrite) {
        ++this->mWrite;
    }
    ++this->mSize;
    if (this->mSize == this->HIGH_SIZE) {
        this->mValid = false;
    }
    return true;
}

template <class PARAM>
int32_t KafkaSendQueue<PARAM>::RemoveItem(LoggroupEntry * item) {
    if (this->IsEmpty()) {
        return 0;
    }

    auto index = item->mIndex;
    auto dataItem = this->mArray[index % this->SIZE];
    if (dataItem == item) {
        this->mArray[index % this->SIZE] = NULL;
    }
    if (index == this->mWrite) {
        LOG_ERROR(
            sLogger,
            ("find no sender item, logstoreKey", item->mLogstoreKey)(
                "lines", item->mLogLines));
        return 0;
    }
    // need to check mWrite to avoid dead while
    while (this->mRead < this->mWrite && this->mArray[this->mRead % this->SIZE] == NULL) {
        ++this->mRead;
    }
    int rst = 1;
    if (--this->mSize == this->LOW_SIZE && !this->mValid) {
        this->mValid = true;
        rst = 2;
    }
    return rst;
}

template <class PARAM>
void KafkaSendQueue<PARAM>::OnSendDone(LoggroupEntry * item, KafkaSendInfo::SendResult sendRst) {
    mSendInfo.RecordSendResult(sendRst, mSendStatistics);
}

template <class PARAM>
bool KafkaSendQueue<PARAM>::IsValidToSend(int32_t curTime) {
    return mSendInfo.CanSend(curTime);
}

template <class PARAM>
KafkaSendStatistics KafkaSendQueue<PARAM>::GetSenderStatistics() {
        KafkaSendStatistics statisticsItem = mSendStatistics;
        mSendStatistics.Reset();

        statisticsItem.mSendBlockFlag = !this->IsValid();
        statisticsItem.mValidToSendFlag = IsValidToSend(time(NULL));
        if (this->IsEmpty()) {
            return statisticsItem;
        }

        uint64_t index = this->mRead;
        int32_t minSendTime = INT32_MAX;
        int32_t maxSendTime = 0;
        for (size_t i = 0; i < this->mSize; ++i, ++index) {
            LoggroupEntry * item = this->mArray[index % this->SIZE];
            if (item == NULL) {
                continue;
            }

            if (item->mCreateTime < minSendTime) {
                minSendTime = item->mCreateTime;
            }
            if (item->mCreateTime > maxSendTime) {
                maxSendTime = item->mCreateTime;
            }
            ++statisticsItem.mSendQueueSize;
        }
        if (statisticsItem.mSendQueueSize > 0) {
            statisticsItem.mMinUnsendTime = minSendTime;
            statisticsItem.mMaxUnsendTime = maxSendTime;
        }
        return statisticsItem;
}

void KafkaProducerQueue::PopAllItem(std::vector<LoggroupEntry *> & itemVec, int32_t curTime) {
    if (mSendQueueMap.empty()) {
        return;
    }

    for (const auto & [key, queue] : mSendQueueMap) {
        queue->GetAllLoggroup(itemVec);
    }
}

void KafkaProducerQueue::CheckAndPopAllItem(std::vector<LoggroupEntry *> & itemVec, int32_t curTime) {
    if (mSendQueueMap.empty()) {
        return;
    }

    // must check index before moving iterator
    mSenderQueueBeginIndex = mSenderQueueBeginIndex % mSendQueueMap.size();

    // here we set sender queue begin index, let the sender order be different each time
    auto beginIter = mSendQueueMap.begin();
    std::advance(beginIter, mSenderQueueBeginIndex++);

    PopItem(beginIter, mSendQueueMap.end(), itemVec, curTime);
    PopItem(mSendQueueMap.begin(), beginIter, itemVec, curTime);
}

void KafkaProducerQueue::PopItem(KafkaSendQueueMapIterator beginIter,
                                 KafkaSendQueueMapIterator endIter,
                                 std::vector<LoggroupEntry*> & itemVec,
                                 int32_t curTime) {
    for (KafkaSendQueueMapIterator iter = beginIter; iter != endIter; ++iter) {
        KafkaSendQueuePtr & singleQueue = iter->second;
        if (singleQueue->IsEmpty()) {
            continue;
        }
        if (!singleQueue->IsValidToSend(curTime)) {
            continue;
        }
        singleQueue->GetAllLoggroupWithLimit(itemVec);
    }
}

void KafkaSenderQueue::SetParam(const size_t lowSize, const size_t highSize, const size_t maxSize) {
    KafkaSenderQueueParam * pParam = KafkaSenderQueueParam::GetInstance();
    pParam->SetLowSize(lowSize);
    pParam->SetHighSize(highSize);
    pParam->SetMaxSize(maxSize);
}

void KafkaSenderQueue::SetMaxQueueSize(const size_t maxQueueSize) {
    mMaxQueueSize = maxQueueSize;
}

void KafkaSenderQueue::SetFeedBackObject(LogstoreFeedBackInterface * pFeedbackObj) {
    PTScopedLock dataLock(mLock);
    mFeedBackObj = pFeedbackObj;
}

void KafkaSenderQueue::Signal() {
    for (const auto & [key, queue] : mKafkaProducerQueue) {
        queue->mTrigger.Trigger();
    }
}

void KafkaSenderQueue::FeedBack(const LogstoreFeedBackKey & key) {
    KafkaSendQueuePtr queue = getKafkaSendQueue(key);
    if (!queue || !queue->mKafkaProducerQueue) {
        return;
    }
    queue->mKafkaProducerQueue->mTrigger.Trigger();
}

bool KafkaSenderQueue::IsValidToPush(const LogstoreFeedBackKey & key, FeedBackStatusPtr status) {
    PTScopedLock dataLock(mLock);
    KafkaSendQueuePtr queue = getKafkaSendQueue(key);
    if (!queue) {
        return false;
    }

    if (mQueueSize > mMaxQueueSize) {
        return false;
    }

    if (mUrgentFlag) {
        return true;
    }

    if (status) {
        status->mSendValidFlag = queue->mSendInfo.mSendValidFlag;
    }

    return queue->IsValid();
}

bool KafkaSenderQueue::Wait(int64_t kafkaProducerKey, int32_t waitMs) {
    KafkaProducerQueuePtr queue = getKafkaProducerQueue(kafkaProducerKey);
    if (!queue) {
        return false;
    }
    return queue->mTrigger.Wait(waitMs);
}

bool KafkaSenderQueue::PushItem(const LogstoreFeedBackKey & key, LoggroupEntry * const & item) {
    KafkaSendQueuePtr queue = nullptr;
    {
        PTScopedLock lock(mLock);
        queue = getKafkaSendQueue(key);
        if (!queue) {
            LOG_ERROR(sLogger, ("This is a bug, kafka send queue cannot be null, logstoreKey", key));
            return false;
        }
        if (!queue->InsertItem(item)) {
            return false;
        }

        ++mQueueSize;
    }
    queue->mKafkaProducerQueue->mTrigger.Trigger();
    return true;
}

void KafkaSenderQueue::CheckAndPopAllItemByKafkaProducerKey(std::vector<LoggroupEntry *> & itemVec, int64_t kafkaProducerKey, int32_t curTime) {
    PTScopedLock lock(mLock);
    KafkaProducerQueuePtr queue = getKafkaProducerQueue(kafkaProducerKey);
    if (queue) {
        queue->CheckAndPopAllItem(itemVec, curTime);
    }
}

void KafkaSenderQueue::PopAllItemByKafkaProducerKey(std::vector<LoggroupEntry *> & itemVec, int64_t kafkaProducerKey, int32_t curTime) {
    PTScopedLock lock(mLock);
    KafkaProducerQueuePtr queue = getKafkaProducerQueue(kafkaProducerKey);
    if (queue) {
        queue->PopAllItem(itemVec, curTime);
    }
}

bool KafkaSenderQueue::IsEmpty() {
    PTScopedLock lock(mLock);
    return mQueueSize == 0;
}

bool KafkaSenderQueue::IsEmpty(const LogstoreFeedBackKey & key) {
    PTScopedLock lock(mLock);
    KafkaSendQueuePtr queue = getKafkaSendQueue(key);
    if (!queue) {
        return true;
    }
    return queue->IsEmpty();
}

void KafkaSenderQueue::SetUrgent() {
    mUrgentFlag = true;
}

void KafkaSenderQueue::ResetUrgent() {
    mUrgentFlag = false;
}

KafkaSendStatistics KafkaSenderQueue::GetSenderStatistics(const LogstoreFeedBackKey & key) {
    PTScopedLock dataLock(mLock);
    KafkaSendQueuePtr queue = getKafkaSendQueue(key);
    if (!queue) {
        return KafkaSendStatistics();
    }
    return queue->GetSenderStatistics();
}


void KafkaSenderQueue::GetStatus(int32_t & invalidCount, int32_t & totalCount, int32_t & queueSize) {
    PTScopedLock dataLock(mLock);
    for (const auto & [key , queue] : mKafkaSendQueue) {
        ++totalCount;
        if (!queue->IsValid()) {
            ++invalidCount;
        }
        queueSize += queue->GetSize();
    }
}

size_t KafkaSenderQueue::GetSize() {
    PTScopedLock dataLock(mLock);
    return mQueueSize;
}

size_t KafkaSenderQueue::GetSize(int64_t kafkaProducerKey) {
    PTScopedLock dataLock(mLock);
    KafkaProducerQueuePtr queue = getKafkaProducerQueue(kafkaProducerKey);
    if (!queue) {
        return 0;
    }

    size_t queueSize = 0;
    for (const auto & [k, p] : queue->mSendQueueMap) {
        queueSize += p->GetSize();
    }
    return queueSize;
}

void KafkaSenderQueue::addSendFailedOffset(LoggroupOffsetMap & fileOffsets) {
    for (const auto & [k, offset]: fileOffsets) {
        auto [it, inserted] = mSendFailedOffsets.emplace(k, offset);
        if (!inserted && offset < it->second) {
            it->second = offset;
        }
    }
}

void KafkaSenderQueue::OnLoggroupSendDone(LoggroupEntry * item, KafkaSendInfo::SendResult sendRst) {
    if (item == NULL) {
        return;
    }

    PTScopedLock dataLock(mLock);
    KafkaSendQueuePtr queue = getKafkaSendQueue(item->mLogstoreKey);
    if (!queue) {
        return;
    }

    switch (sendRst) {
        case KafkaSendInfo::SendResult_OK:
            queue->OnSendDone(item, sendRst);
            mSendingEntrys.erase(item);
            delete item;
            break;
        case KafkaSendInfo::SendResult_SendFail:
        case KafkaSendInfo::SendResult_OtherFail:
            queue->OnSendDone(item, sendRst);
            addSendFailedOffset(item->mFileOffsets);
            mSendingEntrys.erase(item);
            delete item;
            break;
        case KafkaSendInfo::SendResult_Sending:
            removeItem(queue, item);
            mSendingEntrys.insert(item);
            break;
        default:
           break;
    }
}

void KafkaSenderQueue::removeItem(KafkaSendQueuePtr & queue, LoggroupEntry * item) {
    if (item == NULL) {
        return;
    }

    int rst = queue->RemoveItem(item);
    if (rst == 2 && mFeedBackObj != NULL) {
        APSARA_LOG_DEBUG(sLogger, ("OnLoggroupSendDone feedback", ""));
        mFeedBackObj->FeedBack(item->mLogstoreKey);
    }
    if (rst != 0) {
        --mQueueSize;
    }
}

LoggroupOffsetMap KafkaSenderQueue::GetSendFailedOffsets() {
    PTScopedLock dataLock(mLock);
    LoggroupOffsetMap result = std::move(mSendFailedOffsets);
    mSendFailedOffsets.clear();
    return result;
}

void KafkaSenderQueue::CreateKafkaSendQueue(const LogstoreFeedBackKey & key, int64_t kafkaProducerKey) {
    PTScopedLock dataLock(mLock);
    KafkaSendQueuePtr queue = getKafkaSendQueue(key);
    if (queue) {
        /// 处理任务切换Kafka集群
        if (queue->mKafkaProducerQueue->mKafkaProducerKey != kafkaProducerKey) {
            KafkaProducerQueuePtr oldProducerQueue = queue->mKafkaProducerQueue;
            oldProducerQueue->mSendQueueMap.erase(key);
            KafkaProducerQueuePtr producerQueue = getKafkaProducerQueue(kafkaProducerKey);
            if (!producerQueue) {
                producerQueue = std::make_shared<KafkaProducerQueue>(kafkaProducerKey);
                mKafkaProducerQueue[kafkaProducerKey] = producerQueue;
            }
            queue->mKafkaProducerQueue = producerQueue;
            producerQueue->mSendQueueMap[key] = queue;
        }
        return;
    }

    KafkaProducerQueuePtr producerQueue = getKafkaProducerQueue(kafkaProducerKey);
    if (!producerQueue) {
        producerQueue = std::make_shared<KafkaProducerQueue>(kafkaProducerKey);
        mKafkaProducerQueue[kafkaProducerKey] = producerQueue;
    }

    queue = std::make_shared<KafkaSendQueue<KafkaSenderQueueParam>>(producerQueue);
    producerQueue->mSendQueueMap[key] = queue;
    mKafkaSendQueue[key] = queue;
}

KafkaProducerQueuePtr KafkaSenderQueue::getKafkaProducerQueue(int64_t kafkaProducerKey) {
    if (auto it = mKafkaProducerQueue.find(kafkaProducerKey); it != mKafkaProducerQueue.end()) {
        return it->second;
    }
    return nullptr;
}

KafkaSendQueuePtr KafkaSenderQueue::getKafkaSendQueue(const LogstoreFeedBackKey & key) {
    if (auto it = mKafkaSendQueue.find(key); it != mKafkaSendQueue.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace logtail
