#pragma once
#include <QImage>

// Windows UI adapter helper. Decides when a polled subtitle region has stopped changing,
// so OCR runs once per dialogue line instead of on every poll. Plain image logic:
// a candidate for the shared core once mobile screen capture exists.
class ChangeDetector {
public:
    enum class Decision { Wait, Process, Unchanged };
    static constexpr int StableTicks = 2;      // identical polls in a row before OCR
    static constexpr qint64 MaxWaitMs = 3000;  // animated backgrounds never settle
    Decision feed(const QImage& frame, qint64 nowMs);
    void reset();
    static QImage signature(const QImage& frame);
    static bool differs(const QImage& a, const QImage& b);
private:
    QImage previous_;
    QImage processed_;
    int stableTicks_ = 0;
    qint64 changingSince_ = -1;
};
