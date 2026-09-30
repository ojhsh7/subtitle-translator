#pragma once
#include <QImage>

// Windows adapter: compare captured pixels and wait for dialogue to settle.
class ChangeDetector {
public:
    enum class Decision { Wait, Process, Unchanged };
    static constexpr int StableTicks = 2;
    static constexpr qint64 MaxWaitMs = 3000;
    static constexpr qint64 RetryDelayMs = 3000;
    static constexpr int MaxRetries = 2;
    Decision feed(const QImage& frame, qint64 nowMs);
    // Only OCR failure schedules a bounded retry. Never retry a paid API failure here.
    bool retryAfterFailure(qint64 nowMs);
    void reset();
    static QImage signature(const QImage& frame);
    static bool differs(const QImage& a, const QImage& b);
private:
    QImage previous_;
    QImage processed_;
    int stableTicks_ = 0;
    qint64 changingSince_ = -1;
    qint64 retryAt_ = -1;
    int retries_ = 0;
    bool sampledWhileChanging_ = false;
};
