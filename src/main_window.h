#pragma once
#include "change_detector.h"
#include "game_window.h"
#include "ocr_service.h"
#include "translation_service.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QImage>
#include "subtitle/core.h"
#include "subtitle/text.h"
#include <QPointer>
#include <QHash>
#include <functional>
#include <vector>
#include <QRect>
class QComboBox;
class QTimer;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;
class QScreen;
class QCloseEvent;
class SubtitleOverlay;
class CaptureFlash;
class MainWindow : public QMainWindow {
public:
    friend class WindowChecks;
    MainWindow();
    ~MainWindow() override;
    bool autoTranslating() const;
protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent*) override;
private:
    void settings();
    void chooseRegion(QScreen* screen = nullptr); // nullptr: the monitor chosen in the main window
    void chooseRegionUnderCursor();
    void setRegion(QScreen* screen, const QRect& region);
    bool hasRegion() const;
    // The region chosen over a game window follows that window (move, resize, other monitor).
    struct GameRegion {
        QString key;      // program path, GameWindow::Info::key
        QString name;     // program file name for display
        QRect relative;   // physical pixels relative to the client area
        QSize client;     // client size when the region was chosen, physical pixels
        quintptr handle = 0;
    };
    // Recomputes the region from the game window. false: the game window is not visible now.
    bool followGame(bool switchToForeground = true);
    void rememberGame(const GameRegion& game);
    struct CaptureArea {
        QPointer<QScreen> screen;
        QRect screenGeometry;
        QRect rect; // logical pixels relative to screen
        qreal dpr = 1;
    };
    CaptureArea selectedArea() const;
    void startCapture();
    void startCursorCapture();
    void scheduleCursorCapture();
    void setHoverTranslate(bool enabled);
    void hoverTick();
    void updateHover(const QPoint& point, qint64 nowMs, bool mousePressed = false);
    void beginScreenCapture(const CaptureArea& area, const QString& status);
    void importImage();
    void processImage(const QImage& image, int source);
    void resetOutput();
    void cancel();
    void finish(const QString& status);
    void setBusy(bool busy);
    void refreshScreens();
    void restoreWindows();
    void showResult();
    bool moveResultOutside(const QRect& source);
    void stopAutoForResultOverlap();
    QString resultPlacementWarning_;
    void presentTranslation();
    void previewSubtitle();
    bool showSubtitle(const QString& text, bool selectionPreview = false);
    void setAutoTranslate(bool enabled);
    void autoTick();
    bool coversRegion(const QRect& global) const;
    void saveRegion();
    void restoreRegion();
    QString ocrLanguages() const;
    void applyOcrEngine();
    bool ocrReady() const;
    QString targetLanguage() const;
    QComboBox* screens_;
    QComboBox* ocrLanguageBox_;
    QComboBox* targetLanguageBox_;
    QLabel* status_;
    QLabel* regionLabel_;
    QLabel* preview_;
    QPushButton* select_;
    QPushButton* capture_;
    QPushButton* cursorCapture_;
    QComboBox* cursorSizeBox_;
    QCheckBox* followGame_;
    QCheckBox* showCaptureRange_;
    CaptureFlash* captureFlash_;
    GameRegion game_;
    QHash<QString, GameRegion> knownGames_; // every game with a saved region, by key
    quint32 ignoreProcess_ = 0;             // our own process; tests use 0 to treat their windows as games
    std::function<GameWindow::Info()> foregroundWindow_; // the window in front; tests replace it
    QCheckBox* hoverMode_;
    QTimer* hoverTimer_;
    QPoint hoverAnchor_;
    qint64 hoverSince_ = -1;
    bool hoverConsumed_ = false;
    bool hoverRequest_ = false; // Only hover-owned work may be cancelled when the pointer moves.
    QPushButton* settings_;
    QPushButton* cancel_;
    QPushButton* import_;
    QPushButton* retry_;
    QPushButton* auto_;
    QTimer* autoTimer_;
    QElapsedTimer autoClock_;
    // After DeepL answers 429, auto translation sends no request until this autoClock_ time.
    qint64 translationPausedUntil_ = -1;
    static constexpr int MinRateLimitPauseSeconds = 10;
    ChangeDetector detector_;
    QImage lastImage_;
    int lastSource_ = ST_IMAGE_FILE;
    int activeSource_ = ST_IMAGE_FILE;
    st_session* session_ = st_create();
    uint64_t request_ = 0;
    std::vector<subtitle::TextSegment> segments_; // pieces of the current line: kept or sent to DeepL
    QPlainTextEdit* original_;
    QWidget* resultWindow_;
    SubtitleOverlay* subtitleOverlay_;
    QCheckBox* inlineCaptions_;
    QPushButton* captionPreview_;
    QLabel* resultStatus_;
    QPlainTextEdit* result_;
    QPointer<QScreen> selectedScreen_;
    QRect selectedGeometry_;
    qreal selectedDpr_ = 1;
    QRect region_;
    CaptureArea captionArea_;     // current request's anchor, independent of the saved game region
    CaptureArea lastCaptionArea_; // retry must use the last image's original anchor
    OcrService ocr_;
    TranslationService translation_;
    QString executable_;
    QString paddleDir_;        // bundled PaddleOCR models: <app>/ocr/paddle
    bool paddleEngine_ = true; // false: Tesseract only
    QString tessdata_;
    QString apiKey_;
    bool freeApi_ = true;
    bool consent_ = false;
    bool remember_ = false;
    bool busy_ = false;
    bool modal_ = false;
    bool registered_ = false;
    bool autoRegistered_ = false;
    bool cursorRegistered_ = false;
    bool hoverRegistered_ = false;
    bool regionRegistered_ = false;
    QString regionShortcut_ = QStringLiteral("Ctrl+Alt+R");
    QString hoverShortcut_ = QStringLiteral("Ctrl+Alt+H");
    QString cursorShortcut_ = QStringLiteral("Ctrl+Alt+M");
    bool autoPending_ = false;  // --auto launch waiting for setup or a region
    bool restoreMain_ = false;
    bool restoreResult_ = false;
    bool restoreOverlay_ = false;
    int psm_ = 0; // 0 = automatic, 6 = block, 7 = single line
    quint64 generation_ = 0;
};
