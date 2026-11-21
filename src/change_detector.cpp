#include "change_detector.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace {
// An outline that appeared or vanished: clearly present in one map and clearly absent in the
// other. A pixel near the level on both sides (anti-aliasing while the background fades) is not.
constexpr int EdgeAbsent = ChangeDetector::EdgeLevel / 2;
}
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
    const int w = a.width();
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.constScanLine(y);
        const uchar* pb = b.constScanLine(y);
        // Screen captures are lossless: a row that did not change is byte for byte the same, and
        // memcmp skips it many times faster than the loop below (P-38: most of each poll's work).
        if (std::memcmp(pa, pb, size_t(w)) == 0) continue;
        for (int x = 0; x < w; ++x)
            if (std::abs(int(pa[x]) - int(pb[x])) > 24 && ++changed >= 3) return true;
    }
    return false;
}
QImage ChangeDetector::edges(const QImage& signature) {
    QImage map(signature.size(), QImage::Format_Grayscale8);
    map.fill(0);
    const int w = signature.width(), h = signature.height();
    for (int y = 1; y + 1 < h; ++y) {
        const uchar* above = signature.constScanLine(y - 1);
        const uchar* row = signature.constScanLine(y);
        const uchar* below = signature.constScanLine(y + 1);
        uchar* out = map.scanLine(y);
        for (int x = 1; x + 1 < w; ++x)
            out[x] = uchar(std::max(std::abs(int(row[x + 1]) - int(row[x - 1])), std::abs(int(below[x]) - int(above[x]))));
    }
    return map;
}
double ChangeDetector::changedFraction(const QImage& a, const QImage& b) {
    if (a.isNull() || a.size() != b.size()) return 1;
    qint64 changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.constScanLine(y);
        const uchar* pb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x)
            changed += (pa[x] >= EdgeLevel && pb[x] < EdgeAbsent) || (pb[x] >= EdgeLevel && pa[x] < EdgeAbsent);
    }
    return double(changed) / (double(a.width()) * a.height());
}
double ChangeDetector::outlineAmount(const QImage& edges) {
    if (edges.isNull()) return 0;
    qint64 count = 0;
    for (int y = 0; y < edges.height(); ++y) {
        const uchar* row = edges.constScanLine(y);
        for (int x = 0; x < edges.width(); ++x) count += row[x] >= EdgeLevel;
    }
    return double(count) / (double(edges.width()) * edges.height());
}
void ChangeDetector::measureOutline(const QImage& signature, const QImage& reference, QImage& map, double& change, double& ink) {
    // edges(), changedFraction() and outlineAmount() in one pass: this runs on every moving frame.
    map = QImage(signature.size(), QImage::Format_Grayscale8);
    map.fill(0);
    const int w = signature.width(), h = signature.height();
    const bool compare = !reference.isNull() && reference.size() == signature.size();
    qint64 changed = 0, count = 0;
    for (int y = 1; y + 1 < h; ++y) {
        const uchar* above = signature.constScanLine(y - 1);
        const uchar* row = signature.constScanLine(y);
        const uchar* below = signature.constScanLine(y + 1);
        const uchar* old = compare ? reference.constScanLine(y) : nullptr;
        uchar* out = map.scanLine(y);
        for (int x = 1; x + 1 < w; ++x) {
            const int value = std::max(std::abs(int(row[x + 1]) - int(row[x - 1])), std::abs(int(below[x]) - int(above[x])));
            out[x] = uchar(value);
            count += value >= EdgeLevel;
            if (old) changed += (value >= EdgeLevel && old[x] < EdgeAbsent) || (old[x] >= EdgeLevel && value < EdgeAbsent);
        }
    }
    // Border pixels of the reference (always 0 in a map) cannot have changed.
    const double area = double(w) * h;
    change = compare ? double(changed) / area : 1;
    ink = double(count) / area;
}
bool ChangeDetector::wasRead(const QImage& signature) const {
    return std::any_of(read_.begin(), read_.end(), [&](const QImage& seen) { return !differs(signature, seen); });
}
ChangeDetector::Decision ChangeDetector::feed(const QImage& frame, qint64 nowMs) {
    if (frame.isNull()) return Decision::Wait;
    const QImage current = signature(frame);
    if (previous_.isNull()) {
        previous_ = current;
        double ignored = 0;
        measureOutline(current, QImage(), reference_, ignored, ink_);
        inkHigh_ = inkMin_ = inkMax_ = ink_;
        lastActivity_ = lastPixelChange_ = changingSince_ = lastLook_ = lastStillSettle_ = nowMs;
        return Decision::Wait;
    }
    const double smallest = 3.0 / (double(current.width()) * current.height()); // the three pixels of differs()
    const bool still = !differs(current, previous_);
    previous_ = current;
    bool known = false;
    QImage outline;
    auto measure = [&] {
        measureOutline(current, reference_, outline, change_, ink_);
        inkMin_ = std::min(inkMin_, ink_); inkMax_ = std::max(inkMax_, ink_);
    };
    if (!still) {
        lastPixelChange_ = nowMs;
        known = wasRead(current); // a blink back to a frame already read is no news
        if (!known) {
            measure();
            bool active = false;
            // The picture left the frame that was read, by more than the animation seen so far.
            const bool different = change_ > std::max(smallest, tolerance_);
            if (different && !different_) active = true;
            different_ = different;
            // Text arriving adds outline poll after poll; an arrow that moves or lights that drift
            // keep about the same amount. Much less outline at once: the box was cleared.
            const double margin = std::max(smallest, inkNoise_);
            if (ink_ > inkHigh_ + margin || ink_ < inkHigh_ - std::max(4 * margin, 0.25 * inkHigh_)) {
                inkHigh_ = ink_; active = true;
            }
            if (active) { lastActivity_ = nowMs; ++activityPolls_; }
        }
    }
    bool forced = false;
    // One quiet poll between animated frames is not a settled dialogue.
    const qint64 quietFor = nowMs - (still ? lastPixelChange_ : lastActivity_);
    if (quietFor < SettleMs) {
        if (changingSince_ < 0) changingSince_ = nowMs;
        if (nowMs - changingSince_ < MaxWaitMs) return Decision::Wait;
        changingSince_ = nowMs;
        forced = true; // a line typed for longer than MaxWaitMs is read meanwhile
    } else if (still || (!known && quietFor >= 2 * SettleMs)) {
        changingSince_ = -1; // rest, or movement that stayed quiet, ends the changing spell
    }
    bool changed = processed_.isNull(), look = false;
    if (still && !forced) lastStillSettle_ = nowMs;
    if (!changed && still) {
        changed = differs(current, processed_) && !wasRead(current);
        if (changed) {
            if (outline.isNull()) measure();
            // Brightness drifted (a fade, a slow pan) but no outline moved: the text is the same.
            if (change_ <= smallest) changed = false;
            // No more than known animation (the arrow went dark): read it only if it stays.
            else if (!forced && change_ <= tolerance_ && quietFor < SmallChangeSettleMs) changed = false;
        }
    } else if (!changed && !known) {
        changed = change_ > tolerance_;
        // Movement the allowance hides could hide a line too. Where the picture never rests (a
        // resting one has its own check above), look once every MaxWaitMs, as before.
        if (!changed && nowMs - lastLook_ >= MaxWaitMs && nowMs - lastStillSettle_ >= MaxWaitMs) look = true;
    }
    // Reads that keep finding the same text are spaced out, up to the old MaxWaitMs rhythm. A change
    // that looks like a new line (half of what a line changes, and well above the animation) is
    // never held back.
    const bool small = change_ < std::max(0.5 * lineChange_, 2 * animationLevel_);
    if (changed && small && !forced && !processed_.isNull() && sameStreak_ > 0 &&
        nowMs - lastLook_ < std::min<qint64>(MaxWaitMs, qint64(400) << std::min(sameStreak_, 4))) changed = false;
    const bool retry = retryAt_ >= 0 && nowMs >= retryAt_;
    const bool finalRead = sampledWhileChanging_ && !forced;
    if (!changed && !forced && !look && !retry && !finalRead) return Decision::Unchanged;
    if (changed) retries_ = 0;
    if (outline.isNull()) measure();
    // Not for the first read: a line already on screen when watching began may still be typing.
    atOnce_ = !processed_.isNull() && activityPolls_ >= 1 && activityPolls_ <= 2;
    changeAtRead_ = change_;
    inkSpanAtRead_ = inkMax_ - inkMin_;
    uncertainLook_ = look && !changed;
    movingAtRead_ = !still;
    firstRead_ = processed_.isNull();
    processed_ = current;
    reference_ = outline;
    read_.append(current);
    if (read_.size() > RememberedReads) read_.removeFirst();
    change_ = 0; different_ = false; activityPolls_ = 0;
    if (!forced) changingSince_ = -1;
    inkHigh_ = inkMin_ = inkMax_ = ink_;
    lastLook_ = nowMs;
    retryAt_ = -1;
    sampledWhileChanging_ = forced;
    return Decision::Process;
}
void ChangeDetector::noteRead(bool textChanged) {
    if (textChanged) {
        sameStreak_ = 0;
        // The allowance hid this line until the periodic look found it: it was too generous.
        if (uncertainLook_) { tolerance_ *= 0.5; inkNoise_ *= 0.5; }
        // The first read is measured against whatever was on screen when watching began.
        if (!firstRead_ && changeAtRead_ > 0) lineChange_ = lineChange_ > 0 ? (lineChange_ + changeAtRead_) / 2 : changeAtRead_;
        return;
    }
    ++sameStreak_;
    animationLevel_ = std::max(animationLevel_, changeAtRead_);
    // What changed was animation. Allow that much, but never as much as a line changes.
    const double cap = lineChange_ > 0 ? lineChange_ * 0.5 : 0.02;
    tolerance_ = std::min(cap, std::max(tolerance_, changeAtRead_ * 1.5));
    inkNoise_ = std::min(cap, std::max(inkNoise_, inkSpanAtRead_ * 1.2));
}
bool ChangeDetector::retryAfterFailure(qint64 nowMs) {
    if (processed_.isNull() || retryAt_ >= 0 || retries_ >= MaxRetries) return false;
    ++retries_;
    retryAt_ = nowMs + RetryDelayMs;
    return true;
}
void ChangeDetector::reset() { *this = ChangeDetector(); }
