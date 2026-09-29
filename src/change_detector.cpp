#include "change_detector.h"
#include <cstdlib>
QImage ChangeDetector::signature(const QImage& frame) {
    // A small grayscale copy is cheap to compare yet still shows one new character.
    return frame.scaled(160, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}
bool ChangeDetector::differs(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return true;
    // Count clearly changed pixels so a single anti-aliasing flicker is ignored.
    int changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.constScanLine(y);
        const uchar* pb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x)
            if (std::abs(int(pa[x]) - int(pb[x])) > 24 && ++changed >= 2) return true;
    }
    return false;
}
ChangeDetector::Decision ChangeDetector::feed(const QImage& frame, qint64 nowMs) {
    const QImage current = signature(frame);
    if (previous_.isNull() || differs(current, previous_)) {
        // Still changing: typewriter text or a scene transition. Wait for it to settle.
        previous_ = current; stableTicks_ = 0;
        if (changingSince_ < 0) changingSince_ = nowMs;
        if (nowMs - changingSince_ < MaxWaitMs) return Decision::Wait;
        changingSince_ = nowMs;
    } else {
        changingSince_ = -1;
        if (stableTicks_ < StableTicks) ++stableTicks_;
        if (stableTicks_ < StableTicks) return Decision::Wait;
    }
    if (!processed_.isNull() && !differs(current, processed_)) return Decision::Unchanged;
    processed_ = current;
    return Decision::Process;
}
void ChangeDetector::reset() {
    previous_ = QImage(); processed_ = QImage();
    stableTicks_ = 0; changingSince_ = -1;
}
