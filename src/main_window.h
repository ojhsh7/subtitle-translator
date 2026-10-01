#pragma once
#include "capture_worker.h"
#include "history_store.h"
#include "hotkey_manager.h"
#include "mode_profile.h"
#include "ocr_service.h"
#include "translation_service.h"
#include "usage_meter.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QImage>
#include "subtitle/core.h"
#include "subtitle/text.h"
#include <QPointer>
#include <vector>
#include <QRect>
class QComboBox;
class QSpinBox;
class QTimer;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;
class QScreen;
class QCloseEvent;
class QStackedWidget;
class QSystemTrayIcon;
class QTabBar;
class QNetworkAccessManager;
class SubtitleOverlay;
class SubtitlePresenter;
class TranslationQueue;
class HistoryWindow;
class VideoMode;
class TextMode;
class FileMode;
struct ModeContext;
class MainWindow : public QMainWindow {
public:
    friend class WindowChecks;
    friend class VideoChecks;
    friend class ModeChecks;
    MainWindow();
    ~MainWindow() override;
    bool autoTranslating() const;
    // A second launch handed over its command line (single instance).
    void activateFromLaunch(const QStringList& arguments);
    Mode mode() const { return mode_; }
    void setMode(Mode mode);
protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent*) override;
private:
    void settings();
    void firstRun();
    void chooseRegion();
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
    void minimizeForCapture();
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
    // Shell: modes, hotkeys, tray, usage, history.
    QWidget* buildGamePage();
    QWidget* page(Mode mode);
    ModeContext context();
    void stopScreenWork(); // one screen-watching activity at a time
    void registerHotkeys();
    void applyHotkeyTexts();
    void createTray();
    void updateTray();
    void updateUsage();
    void showHistory();
    void toast(const QString& text);
    QString translationBlocked() const;
    void loadLanguages(Mode mode);
    void hideOwnWindows();
    void applyCaptureExclusion();
    QComboBox* screens_;
    QComboBox* ocrLanguageBox_;
    QComboBox* targetLanguageBox_;
    QLabel* status_;
    QLabel* regionLabel_;
    QLabel* preview_;
    QPushButton* select_;
    QPushButton* capture_;
    QPushButton* cursorCapture_;
    QLabel* cursorShortcutLabel_ = nullptr;
    QLabel* note_ = nullptr;
    QComboBox* cursorSizeBox_;
    QSpinBox* cursorWidth_;
    QSpinBox* cursorHeight_;
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
    CaptureWorker captureWorker_;
    quint64 autoTicket_ = 0;
    QImage lastImage_;
    int lastSource_ = ST_IMAGE_FILE;
    int activeSource_ = ST_IMAGE_FILE;
    st_session* session_ = st_create();
    uint64_t request_ = 0;
    std::vector<subtitle::TextSegment> segments_; // pieces of the current line: kept or sent to DeepL
    QStringList sentPieces_;
    QPlainTextEdit* original_;
    QWidget* resultWindow_;
    SubtitlePresenter* presenter_;
    SubtitleOverlay* subtitleOverlay_; // the presenter's caption window
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
    UsageMeter usage_;
    HistoryStore history_;
    TranslationQueue* queue_ = nullptr;
    QNetworkAccessManager* queueTransport_ = nullptr; // tests: loopback fixture
    HotkeyManager hotkeys_;
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
    QString hoverShortcut_ = QStringLiteral("Ctrl+Alt+H");
    QString cursorShortcut_ = QStringLiteral("Ctrl+Alt+M");
    bool autoPending_ = false;  // --auto launch waiting for setup or a region
    bool restoreMain_ = false;
    bool restoreResult_ = false;
    bool restoreOverlay_ = false;
    int psm_ = 0; // 0 = automatic, 6 = block, 7 = single line
    quint64 generation_ = 0;
    // Shell
    Mode mode_ = Mode::Game;
    QTabBar* modeTabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QWidget* gamePage_ = nullptr;
    VideoMode* videoMode_ = nullptr;
    TextMode* textMode_ = nullptr;
    FileMode* fileMode_ = nullptr;
    QLabel* usageLabel_ = nullptr;
    QPointer<HistoryWindow> historyWindow_;
    QSystemTrayIcon* tray_ = nullptr;
    quint64 gameHistoryId_ = 0;
};
