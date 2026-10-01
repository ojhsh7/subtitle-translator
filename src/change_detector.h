#pragma once
#include <QImage>

// Windows adapter: compare captured pixels and wait for dialogue to settle.
class ChangeDetector {
public:
    enum class Decision { Wait, Process, Unchanged, Blank };
    static constexpr int StableTicks = 2;
    static constexpr qint64 MaxWaitMs = 3000;
    static constexpr qint64 RetryDelayMs = 3000;
    static constexpr int MaxRetries = 2;
    // Game dialogue (default): raw pixels, two quiet polls, forced read after 3 s of change.
    // Video subtitles: compare text outlines only (a moving picture behind the line is ignored),
    // react after one quiet poll when the line is replaced, but wait for words still being added
    // (auto-generated captions grow word by word), and report a cleared line as Blank.
    struct Profile {
        int stableTicks = StableTicks;
        qint64 maxWaitMs = MaxWaitMs;
        bool textMask = false;
        qint64 growthSettleMs = 0;
    };
    static Profile videoProfile() { return {1, 2000, true, 700}; }
    ChangeDetector() = default;
    explicit ChangeDetector(const Profile& profile) : profile_(profile) {}
    void setProfile(const Profile& profile) { profile_ = profile; reset(); }
    const Profile& profile() const { return profile_; }
    Decision feed(const QImage& frame, qint64 nowMs);
    // Only OCR failure schedules a bounded retry. Never retry a paid API failure here.
    bool retryAfterFailure(qint64 nowMs);
    void reset();
    static QImage signature(const QImage& frame);
    static bool differs(const QImage& a, const QImage& b);
    // Text outline mask: strong local contrast (glyph edges and outlines) as 255, else 0.
    static QImage textMask(const QImage& grey);
    struct MaskChange { int removed = 0, added = 0, before = 0, after = 0; };
    // Ink that disappeared / appeared, tolerating one pixel of jitter.
    static MaskChange compareMasks(const QImage& before, const QImage& after);
    static bool masksDiffer(const MaskChange& change);
    static bool isGrowth(const MaskChange& change);
    static bool isBlankMask(const QImage& mask);
private:
    bool changed(const QImage& a, const QImage& b) const;
    Profile profile_;
    QImage previous_;
    QImage processed_;
    int stableTicks_ = 0;
    qint64 changingSince_ = -1;
    qint64 lastChangeAt_ = -1;
    qint64 retryAt_ = -1;
    int retries_ = 0;
    bool sampledWhileChanging_ = false;
    bool processedBlank_ = false;
};
