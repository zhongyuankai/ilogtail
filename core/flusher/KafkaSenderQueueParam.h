#pragma once

namespace logtail {

class KafkaSenderQueueParam {
public:
    KafkaSenderQueueParam() : SIZE(100), LOW_SIZE(10), HIGH_SIZE(20) {}

    static KafkaSenderQueueParam* GetInstance() {
        static auto sQueueParam = new KafkaSenderQueueParam;
        return sQueueParam;
    }

    size_t GetLowSize() { return LOW_SIZE; }

    size_t GetHighSize() { return HIGH_SIZE; }

    size_t GetMaxSize() { return SIZE; }

    void SetLowSize(size_t lowSize) { LOW_SIZE = lowSize; }

    void SetHighSize(size_t highSize) { HIGH_SIZE = highSize; }

    void SetMaxSize(size_t maxSize) { SIZE = maxSize; }

    size_t SIZE;
    size_t LOW_SIZE;
    size_t HIGH_SIZE;
};

} // namespace logtail
