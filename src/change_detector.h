#pragma once
#include <QImage>

// Windows adapter: compare captured pixels and wait for dialogue to settle.
class ChangeDetector {
public:
    enum class Decision { Wait, Process, Unchanged };
    // Polled every PollMs; a frame is read once it stayed the same for SettleMs, two polls
    // (P-31: 400 ms polls and 800 ms before; docs/plan/2026-11-02-translation-latency.ko.md).
    static constexpr int PollMs = 150;
    static constexpr qint64 SettleMs = 300;
    // A read that looks like an unfinished sentence is translated only after this much stillness,
    // the earlier wait, so a typewriter pause at a comma does not send half a line.
    static constexpr qint64 SentenceSettleMs = 800;
    static constexpr qint64 MaxWaitMs = 3000;
    static constexpr qint64 RetryDelayMs = 3000;
    static constexpr int MaxRetries = 2;
    Decision feed(const QImage& frame, qint64 nowMs);
    // How long the frame last fed has looked the same; 0 right after a change.
    qint64 stableMs(qint64 nowMs) const { return lastChange_ < 0 ? 0 : nowMs - lastChange_; }
    // Only OCR failure schedules a bounded retry. Never retry a paid API failure here.
    bool retryAfterFailure(qint64 nowMs);
    void reset();
    static QImage signature(const QImage& frame);
    static bool differs(const QImage& a, const QImage& b);
private:
    QImage previous_;
    QImage processed_;
    qint64 lastChange_ = -1;
    qint64 changingSince_ = -1;
    qint64 retryAt_ = -1;
    int retries_ = 0;
    bool sampledWhileChanging_ = false;
};
