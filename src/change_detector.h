#pragma once
#include <QImage>
#include <QList>

// Windows adapter: compare captured pixels and wait for dialogue to settle.
//
// Two ways a frame counts as settled (P-38, docs/plan/2026-11-15-auto-efficiency.ko.md):
// - still: no pixel moved for SettleMs. This is the whole story on a motionless dialogue box.
// - quiet: pixels keep moving (an arrow that bobs, a scene behind a translucent box) but the amount
//   of outline in the picture stopped rising. Text being typed adds outline every poll; an arrow
//   that moves or lights that drift keep about the same amount.
// A frame already read (a blinking arrow has two) is not read again. After a read that found the
// same text, noteRead(false) tells the detector that the change it saw was animation; that much
// change is then allowed without another read.
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
    // A still frame that differs from the read one by no more than known animation is read only
    // after it stayed this long: longer than a blink, short enough for a real one-character change.
    static constexpr qint64 SmallChangeSettleMs = 1200;
    static constexpr qint64 RetryDelayMs = 3000;
    static constexpr int MaxRetries = 2;
    static constexpr int RememberedReads = 4;
    // Brightness step between neighbours that counts as an outline: the same step differs() calls a
    // changed pixel, so text that differs() can see always has an outline. A panning background or
    // a fade is far below it.
    static constexpr int EdgeLevel = 25;
    Decision feed(const QImage& frame, qint64 nowMs);
    // How long since the picture last left the read frame or gained outline; 0 right after a change.
    qint64 stableMs(qint64 nowMs) const { return lastActivity_ < 0 ? 0 : nowMs - lastActivity_; }
    // After Process: the change arrived in one or two polls (a line shown at once), not typed out.
    bool appearedAtOnce() const { return atOnce_; }
    // After the read that Process asked for: did it find other text than the read before?
    void noteRead(bool textChanged);
    double allowance() const { return tolerance_; }
    // Read once more at atMs whatever the picture shows then: a reading that looked like a
    // misread of the line on screen is sent only if a second read agrees (P-38).
    void lookAgain(qint64 atMs) { retryAt_ = atMs; }
    static constexpr qint64 ConfirmReadMs = 400;
    // Only OCR failure schedules a bounded retry. Never retry a paid API failure here.
    bool retryAfterFailure(qint64 nowMs);
    void reset();
    static QImage signature(const QImage& frame);
    static bool differs(const QImage& a, const QImage& b);
    // Outline strength map of a signature; the share of pixels where an outline appeared or
    // vanished between two maps; the share of pixels that are outline (all 0..1).
    static QImage edges(const QImage& signature);
    static double changedFraction(const QImage& edgesA, const QImage& edgesB);
    static double outlineAmount(const QImage& edges);
private:
    bool wasRead(const QImage& signature) const;
    QImage previous_;
    QImage processed_;
    QImage reference_;        // outline map of processed_, or of the first frame before any read
    QList<QImage> read_;      // signatures of the last RememberedReads frames read
    qint64 lastActivity_ = -1;    // the outline last grew
    qint64 lastPixelChange_ = -1; // any pixel last moved
    qint64 changingSince_ = -1;
    qint64 lastLook_ = -1;
    qint64 lastStillSettle_ = -1; // the picture last rested for SettleMs
    qint64 retryAt_ = -1;
    int retries_ = 0;
    int sameStreak_ = 0;      // reads in a row that found the same text
    bool sampledWhileChanging_ = false;
    bool atOnce_ = false;
    bool uncertainLook_ = false; // the last Process was a look without a seen change
    bool firstRead_ = true;
    bool different_ = false;  // the current frame is away from reference_ by more than tolerance_
    int activityPolls_ = 0;   // polls with activity since the last read
    double change_ = 0;       // outline change of the current frame against reference_
    double ink_ = 0;          // outline amount of the current frame
    double inkHigh_ = 0;      // level ink_ has to pass to count as text arriving
    double inkMin_ = 0, inkMax_ = 0; // range of ink_ since the last read
    double changeAtRead_ = 0, inkSpanAtRead_ = 0;
    double lineChange_ = 0;   // typical outline change of a read that found new text
    double tolerance_ = 0;    // change allowed without a read: animation seen with unchanged text
    double inkNoise_ = 0;     // how much ink_ moves through animation alone
    double animationLevel_ = 0; // largest change that a read showed to be animation
};
