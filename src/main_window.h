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
#include <memory>
#include <vector>
#include <QRect>
class QAbstractButton;
class QAction;
class QEvent;
class CaptionStage;
class QComboBox;
class QDialog;
class QIcon;
class QKeySequence;
class QListWidget;
class QTableWidget;
class QSystemTrayIcon;
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
    // The app icon, drawn in code (no image files): taskbar, tray and windows.
    static QIcon appIcon();
    // Global shortcut ids for RegisterHotKey; defaults are Ctrl+Alt+ the letter in hotkeySpecs.
    enum Hotkey { CaptureKey = 1, AutoKey, CursorKey, HoverKey, RegionKey, CaptionKey };
    void showFromTray();  // also what a second launch asks the running app to do
    // A shortcut RegisterHotKey can take: one key with modifiers. Letters and digits need Ctrl or Alt
    // so typing in a game never triggers them; F1-F24 may stand alone.
    static bool nativeKey(const QKeySequence& sequence, unsigned& modifiers, unsigned& virtualKey);
protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent* event) override;
private:
    // main_window_ui.cpp: windows, tray, settings pages and shortcuts.
    void buildUi();
    void buildSettings();
    void buildTray();
    void registerHotkeys();      // (re)registers every shortcut from the settings
    void refreshUi();            // state badge, button texts, tray menu after any state change
    void setStatus(const QString& text);
    QString withShortcuts(QString text) const; // default key names in a message -> the keys in use
    void refreshIcons();         // icons are drawn in the text colour: again after a light/dark switch
    void quitApp();
    void settings(int page = -1); // -1: the last page shown
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
    // P-31 (docs/plan/2026-11-02-translation-latency.ko.md): translation pipeline.
    void requestTranslation(const QStringList& pieces, unsigned scripts);
    void applyTranslation(const QStringList& translations, const QString& detected, bool remembered);
    void dropHeld();        // the held line changed or was cancelled: release its core request
    void warmTranslation(); // opens the DeepL connection ahead of a likely request
    // Ends like a finished sentence (。！？.!?」』… and the like); other lines wait SentenceSettleMs.
    static bool looksFinished(const QString& text);
    // P-33 (docs/plan/2026-11-02-mvp-gaps.ko.md): what a player missed in the first version.
    void addToBacklog(const QString& source, const QString& translation);
    void showBacklog();
    void loadFixes();
    QString applyFixes(QString text, bool onTranslation) const;
    void restoreGameLanguage(); // the source language saved with the game now in front
    void refreshUsage();        // DeepL /v2/usage: consumes no characters
    // P-36 (docs/plan/2026-11-02-priorities.ko.md)
    void setCaptionsHidden(bool hidden);
    // P-37: extra regions read with the main one (a choice menu, a name box, a second text window).
    void addExtraArea();                                  // asks for a rectangle on the main region's monitor
    void addExtraAreaAt(QScreen* screen, const QRect& rect);
    void clearExtraAreas();
    void saveExtraAreas() const;
    void restoreExtraAreas();
    void showAllAreas();                                  // outlines every region for a moment
    QList<QRect> areaRects(int except) const;             // global rects of the regions in use, but `except`
    SubtitleOverlay* activeOverlay() const;
    int activeExtraCount() const;
    QString diagnostics() const; // states and times for a problem report; never the text read or translated
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
    QLabel* stateBadge_ = nullptr;
    CaptionStage* stage_ = nullptr;        // the last line as it looks over the game
    QWidget* detailsWindow_ = nullptr;     // capture image and OCR text, from the more menu
    std::vector<std::pair<QAbstractButton*, char16_t>> glyphButtons_;
    QListWidget* settingsPages_ = nullptr;
    QDialog* settingsWindow_ = nullptr;
    std::function<void()> reloadSettings_; // fills the settings fields from the current values
    std::function<bool()> saveSettings_;   // applies key, engine and shortcut fields; false keeps the window open
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayAuto_ = nullptr;
    QAction* trayHover_ = nullptr;
    bool quitting_ = false;
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
    struct ExtraArea {
        ~ExtraArea();
        QString gameKey;                 // the game it belongs to; empty when it is a plain screen region
        QPointer<QScreen> screen;
        QRect screenGeometry;
        qreal dpr = 1;
        QRect rect;                      // where it is now: logical pixels, relative to the screen
        QRect relative;                  // physical pixels relative to the game's client area
        QSize client;
        ChangeDetector detector;
        SubtitleOverlay* overlay = nullptr;
    };
    std::vector<std::unique_ptr<ExtraArea>> extras_;
    static constexpr int MaxExtraAreas = 2;
    int activeArea_ = 0;                 // the read in progress: 0 the main region, n extras_[n - 1]
    bool extraInUse(const ExtraArea& extra) const; // belongs to the game in front (or no game) and has a place
    QLabel* extrasLabel_ = nullptr;
    QAction* moreCaptions_ = nullptr;
    QAction* clearExtras_ = nullptr;
    bool holding_ = false; // an unfinished-looking line, already read, waits for the frame to stay still
    QStringList heldPieces_;
    unsigned heldScripts_ = 0;
    // Recent translations by target, source language and pieces; memory only, gone when the app closes.
    struct Remembered { QStringList translations; QString detected; };
    QHash<QString, Remembered> translationCache_;
    QStringList cacheOrder_; // oldest first
    QString pendingCacheKey_;
    static constexpr int CacheLines = 200;
    // Lines shown on screen, oldest first; memory only, gone when the app closes (no saved history).
    struct BacklogLine { QString time, source, translation; };
    QList<BacklogLine> backlog_;
    static constexpr int BacklogLines = 200;
    QWidget* backlogWindow_ = nullptr;
    QListWidget* backlogList_ = nullptr;
    // The user's fixes: on the original before translation, or on the translation before showing it.
    struct TextFix { bool onTranslation = true; QString find, replace; };
    QList<TextFix> fixes_;
    TranslationService usage_; // a connection of its own, so a check never waits for a translation
    qint64 usageUsed_ = -1, usageLimit_ = -1;
    int sentSinceUsage_ = 0;
    static constexpr int UsageEveryRequests = 50;
    QAction* usageAction_ = nullptr;
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
    bool captionsRegistered_ = false;
    QString captionsShortcut_ = QStringLiteral("Ctrl+Alt+V");
    bool captionsHidden_ = false;   // translation goes on; only the on-screen caption is held back
    QAction* trayCaptions_ = nullptr;
    bool contextOn_ = true;         // send the line before as DeepL context (not billed)
    QString contextLine_;           // the original of the last line sent
    int hoverDwellMs_ = 1000;
    QString captureShortcut_ = QStringLiteral("Ctrl+Alt+T");
    QString autoShortcut_ = QStringLiteral("Ctrl+Alt+P");
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
