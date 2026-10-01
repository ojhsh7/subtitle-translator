#include "change_detector.h"
#include <algorithm>
#include <cstdlib>
#include <vector>
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
namespace {
// Separable square minimum/maximum filter on a 0/255 mask (radius r: window 2r+1).
std::vector<uchar> morph(const std::vector<uchar>& in, int w, int h, int r, bool maximum) {
    std::vector<uchar> rows(in.size()), out(in.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uchar v = maximum ? 0 : 255;
            for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r); ++k)
                v = maximum ? std::max(v, in[size_t(y) * w + k]) : std::min(v, in[size_t(y) * w + k]);
            rows[size_t(y) * w + x] = v;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uchar v = maximum ? 0 : 255;
            for (int k = std::max(0, y - r); k <= std::min(h - 1, y + r); ++k)
                v = maximum ? std::max(v, rows[size_t(k) * w + x]) : std::min(v, rows[size_t(k) * w + x]);
            out[size_t(y) * w + x] = v;
        }
    return out;
}
}
QImage ChangeDetector::textMask(const QImage& grey) {
    // Subtitles are thin light strokes against a dark outline, shadow or box. Their own pixels do not
    // change when the picture behind them moves, while the outer edge of the outline does. Keep light
    // pixels that belong to thin strokes (a 7 px opening removes large light areas of the picture)
    // and have a dark pixel within 2 px.
    constexpr int Light = 185, Dark = 70;
    const int w = grey.width(), h = grey.height();
    std::vector<uchar> light(size_t(w) * h), dark(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uchar* row = grey.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            light[size_t(y) * w + x] = row[x] >= Light ? 255 : 0;
            dark[size_t(y) * w + x] = row[x] <= Dark ? 255 : 0;
        }
    }
    const auto wide = morph(morph(light, w, h, 3, false), w, h, 3, true); // opening: large light areas
    const auto nearDark = morph(dark, w, h, 2, true);
    QImage mask(grey.size(), QImage::Format_Grayscale8);
    int count = 0;
    for (int y = 0; y < h; ++y) {
        uchar* out = mask.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t at = size_t(y) * w + x;
            out[x] = light[at] && !wide[at] && nearDark[at] ? 255 : 0;
            count += out[x] != 0;
        }
    }
    if (count >= std::max(20, int(qint64(w) * h / 2000))) return mask;
    // No light-on-dark text: unknown caption style (dark text on a light band) or no caption.
    // Fall back to hard edges so a change is still noticed; OCR then decides what is there.
    mask.fill(0);
    for (int y = 0; y < h; ++y) {
        const uchar* row = grey.constScanLine(y);
        const uchar* below = y + 1 < h ? grey.constScanLine(y + 1) : nullptr;
        uchar* out = mask.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int step = x + 1 < w ? std::abs(int(row[x]) - int(row[x + 1])) : 0;
            if (below) step = std::max(step, std::abs(int(row[x]) - int(below[x])));
            if (step >= 120) out[x] = 255;
        }
    }
    return mask;
}
namespace {
// 3x3 maximum: one pixel of tolerance for compression jitter and sub-pixel movement.
std::vector<uchar> dilate(const QImage& mask) {
    const int w = mask.width(), h = mask.height();
    std::vector<uchar> rows(size_t(w) * h, 0), out(size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const uchar* in = mask.constScanLine(y);
        for (int x = 0; x < w; ++x)
            rows[size_t(y) * w + x] = std::max({in[x], x > 0 ? in[x - 1] : uchar(0), x + 1 < w ? in[x + 1] : uchar(0)});
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out[size_t(y) * w + x] = std::max({rows[size_t(y) * w + x], y > 0 ? rows[size_t(y - 1) * w + x] : uchar(0),
                                               y + 1 < h ? rows[size_t(y + 1) * w + x] : uchar(0)});
    return out;
}
int ink(const QImage& mask) {
    int count = 0;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) count += row[x] != 0;
    }
    return count;
}
}
ChangeDetector::MaskChange ChangeDetector::compareMasks(const QImage& before, const QImage& after) {
    MaskChange change;
    change.before = ink(before); change.after = ink(after);
    if (before.size() != after.size()) { change.removed = change.before; change.added = change.after; return change; }
    const int w = before.width();
    const auto grownBefore = dilate(before), grownAfter = dilate(after);
    for (int y = 0; y < before.height(); ++y) {
        const uchar* b = before.constScanLine(y);
        const uchar* a = after.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            change.removed += b[x] && !grownAfter[size_t(y) * w + x];
            change.added += a[x] && !grownBefore[size_t(y) * w + x];
        }
    }
    return change;
}
bool ChangeDetector::masksDiffer(const MaskChange& c) {
    return c.removed + c.added > std::max(12, std::max(c.before, c.after) * 3 / 100);
}
bool ChangeDetector::isGrowth(const MaskChange& c) {
    // Earlier words stay where they were and new ink appears beside them.
    return c.before >= 30 && c.added > std::max(12, c.before * 3 / 100) && c.removed <= std::max(6, c.before * 8 / 100);
}
bool ChangeDetector::isBlankMask(const QImage& mask) {
    return ink(mask) < std::max(20, int(qint64(mask.width()) * mask.height() / 2000));
}
bool ChangeDetector::changed(const QImage& a, const QImage& b) const {
    return profile_.textMask ? masksDiffer(compareMasks(a, b)) : differs(a, b);
}
ChangeDetector::Decision ChangeDetector::feed(const QImage& frame, qint64 nowMs) {
    if (frame.isNull()) return Decision::Wait;
    QImage current = signature(frame);
    if (profile_.textMask) current = textMask(current);
    bool forced = false;
    if (previous_.isNull() || changed(current, previous_)) {
        previous_ = current; stableTicks_ = 0; lastChangeAt_ = nowMs;
        if (changingSince_ < 0) changingSince_ = nowMs;
        if (nowMs - changingSince_ < profile_.maxWaitMs) return Decision::Wait;
        changingSince_ = nowMs;
        forced = true;
    } else {
        if (stableTicks_ < profile_.stableTicks) ++stableTicks_;
        if (stableTicks_ < profile_.stableTicks) return Decision::Wait;
        // One quiet poll between animated frames is not a settled dialogue.
        changingSince_ = -1;
    }
    const bool differsFromProcessed = processed_.isNull() || changed(current, processed_);
    const bool retry = retryAt_ >= 0 && nowMs >= retryAt_;
    const bool finalRead = sampledWhileChanging_ && !forced;
    if (!differsFromProcessed && !retry && !finalRead) return Decision::Unchanged;
    if (profile_.textMask && !forced) {
        if (isBlankMask(current)) {
            // The line was cleared: tell the caller without spending an OCR run on an empty band.
            processed_ = current; processedBlank_ = true;
            retryAt_ = -1; retries_ = 0; sampledWhileChanging_ = false;
            return Decision::Blank;
        }
        if (differsFromProcessed && !processed_.isNull() && !processedBlank_ &&
            isGrowth(compareMasks(processed_, current)) && nowMs - lastChangeAt_ < profile_.growthSettleMs)
            return Decision::Wait; // words are still being added; read the finished line once
    }
    if (differsFromProcessed) retries_ = 0;
    processed_ = current; processedBlank_ = false;
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
    stableTicks_ = 0; changingSince_ = -1; lastChangeAt_ = -1;
    retryAt_ = -1; retries_ = 0; sampledWhileChanging_ = false; processedBlank_ = false;
}
