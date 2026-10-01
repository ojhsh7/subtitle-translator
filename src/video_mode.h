#pragma once
#include "capture_worker.h"
#include "mode_context.h"
#include "mode_profile.h"
#include "subtitle/text.h"
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <vector>
class QComboBox;
class QLabel;
class QPushButton;
class QScreen;

// Video subtitle mode: watches the subtitle band of a player (YouTube, news, trailers), reads each
// new line, translates it and shows the translation over or next to the original line.
// Reading and translating overlap: while one line is translated the next one is already read,
// so lines of 1-4 s are not skipped while waiting for the network.
class VideoMode final : public QWidget {
    Q_OBJECT
public:
    explicit VideoMode(const ModeContext& context, QWidget* parent = nullptr);
    ~VideoMode() override;
    bool watching() const { return watching_; }
    void setWatching(bool on);
    void toggle() { setWatching(!watching_); }
    bool hasRegion() const { return region_.valid(); }
    const SavedRegion& region() const { return region_; }
    void setRegion(const SavedRegion& region); // also saved for the next launch
    void fitToWindow();     // 3 s to point at the player, then suggest its bottom band
    void bottomOfScreen();  // suggest the bottom 20 % of the screen under the cursor
    void chooseRegion();    // draw it freely
    void previewCaption();
    // Measurements for the acceptance harness and the diagnostics log.
    struct LineTiming {
        qint64 capturedAt = -1, ocrDoneAt = -1, translatedAt = -1, shownAt = -1;
        QString source, translation;
    };
    const QList<LineTiming>& timings() const { return timings_; }
    static constexpr int TickMs = 250;
    static constexpr int LingerMs = 5000;   // translation stays this long after the line disappears
    static constexpr qint64 ProtectedMs = 6000;
signals:
    void watchingChanged(bool on);
private:
    friend class VideoChecks;
    struct Request { quint64 line = 0; std::vector<subtitle::TextSegment> segments; int timing = -1; quint64 history = 0; };
    void buildUi();
    void tick();
    void onFrame(const QImage& image, qint64 capturedAt, quint64 ticket);
    void onBlank(qint64 capturedAt, quint64 ticket);
    void startOcr(const QImage& image, qint64 capturedAt);
    void onRecognized(const QString& text);
    void onOcrFailed(const QString& error);
    void handleText(const QString& text, qint64 capturedAt, qint64 ocrDoneAt);
    void lineCleared(qint64 at);
    void endLine(qint64 at);
    void onTranslated(quint64 id, const QStringList& translations);
    void onTranslateFailed(quint64 id, const QString& error, bool userAction);
    void show(const QString& text);
    void selectOn(QScreen* screen, const QRect& suggestion, const QRect& probe);
    void applyStyle();
    void updateUi();
    ModeContext ctx_;
    CaptureWorker worker_;
    QTimer tick_;
    SavedRegion region_;
    SubtitleOverlay::Style style_;
    bool watching_ = false;
    bool selecting_ = false;
    quint64 ticket_ = 0;
    bool ocrBusy_ = false;
    qint64 ocrStartedAt_ = -1, ocrCapturedAt_ = -1;
    QImage pendingImage_;
    qint64 pendingCapturedAt_ = -1;
    QString lastSource_;     // text of the line on screen now; empty when the band is clear
    QString previousSource_; // context for the next translation
    quint64 line_ = 0;       // increases with every new or cleared line
    quint64 historyId_ = 0;
    QHash<quint64, Request> requests_;
    QList<LineTiming> timings_;
    qint64 blackSince_ = -1;
    bool protectedHintShown_ = false;
    QLabel* regionLabel_ = nullptr;
    QLabel* original_ = nullptr;
    QLabel* translated_ = nullptr;
    QPushButton* start_ = nullptr;
    QComboBox* placement_ = nullptr;
    QComboBox* font_ = nullptr;
    QComboBox* background_ = nullptr;
};
