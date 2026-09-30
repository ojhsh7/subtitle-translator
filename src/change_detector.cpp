#include "change_detector.h"
#include <cstdlib>
QImage ChangeDetector::signature(const QImage& frame) {
    // A 160 px thumbnail erased punctuation and small glyphs in a large dialogue box.
    // Keep more local detail, never upscale, and still bound comparison memory.
    const QImage bounded = frame.width() > 960 || frame.height() > 320
        ? frame.scaled(960, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation) : frame;
    return bounded.convertToFormat(QImage::Format_Grayscale8);
}
bool ChangeDetector::differs(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return true;
    int changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.constScanLine(y);
        const uchar* pb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x)
            if (std::abs(int(pa[x]) - int(pb[x])) > 24 && ++changed >= 3) return true;
    }
    return false;
}
ChangeDetector::Decision ChangeDetector::feed(const QImage& frame, qint64 nowMs) {
    if (frame.isNull()) return Decision::Wait;
    const QImage current = signature(frame);
    bool forced = false;
    if (previous_.isNull() || differs(current, previous_)) {
        previous_ = current; stableTicks_ = 0;
        if (changingSince_ < 0) changingSince_ = nowMs;
        if (nowMs - changingSince_ < MaxWaitMs) return Decision::Wait;
        changingSince_ = nowMs;
        forced = true;
    } else {
        if (stableTicks_ < StableTicks) ++stableTicks_;
        if (stableTicks_ < StableTicks) return Decision::Wait;
        // One quiet poll between animated frames is not a settled dialogue.
        changingSince_ = -1;
    }
    const bool changed = processed_.isNull() || differs(current, processed_);
    const bool retry = retryAt_ >= 0 && nowMs >= retryAt_;
    const bool finalRead = sampledWhileChanging_ && !forced;
    if (!changed && !retry && !finalRead) return Decision::Unchanged;
    if (changed) retries_ = 0;
    processed_ = current;
    retryAt_ = -1;
    sampledWhileChanging_ = forced;
    return Decision::Process;
}
bool ChangeDetector::retryAfterFailure(qint64 nowMs) {
    if (processed_.isNull() || retryAt_ >= 0 || retries_ >= MaxRetries) return false;
    ++retries_;
    retryAt_ = nowMs + RetryDelayMs;
    return true;
}
void ChangeDetector::reset() {
    previous_ = QImage(); processed_ = QImage();
    stableTicks_ = 0; changingSince_ = -1;
    retryAt_ = -1; retries_ = 0; sampledWhileChanging_ = false;
}
