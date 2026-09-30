#include "main_window.h"
#include "capture_service.h"
#include "paddle_ocr.h"
#include <QTemporaryDir>
#include <QFile>
#include <QCursor>
#include <QScopeGuard>
#include "region_selector.h"
#include "subtitle_overlay.h"
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QDir>
#include <QtTest>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QPainter>
#include <QTimer>
#include <windows.h>
class WindowChecks : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qApp->setProperty("testMode",true);
        qApp->setOrganizationName("SubtitleTranslatorChecks");
        qApp->setApplicationName("TemporaryUiChecks");
        qApp->setQuitOnLastWindowClosed(false);
        QSettings().setValue("display/inlineCaptions", true);
    }
    void init() {
        // Language selection saves settings immediately. Isolate each test in the test-only namespace.
        QSettings().clear();
        QSettings().setValue("display/inlineCaptions", true);
    }
    void cleanupTestCase() { QSettings().clear(); }
    void actualImagePipelineAndRetry() {
        if(qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        window.tessdata_=qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        window.apiKey_.clear(); window.consent_=false;
        QImage image(1000,180,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter p(&image); QFont f("Yu Gothic"); f.setPixelSize(52); p.setFont(f);
        p.drawText(image.rect(),Qt::AlignCenter,QStringLiteral("今日はいい天気です。")); p.end();
        window.processImage(image,ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,25000);
        QVERIFY(window.original_->toPlainText().contains(QStringLiteral("天気")));
        QCOMPARE(st_state(window.session_),int(ST_COMPLETE));
        QVERIFY(window.result_->toPlainText().isEmpty());
        QVERIFY(window.retry_->isEnabled());
        window.retry_->click();
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,25000);
        QVERIFY(window.original_->toPlainText().contains(QStringLiteral("天気")));
    }
    void actualScreenCaptureThroughHotkeyMessage() {
        if(qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:52px;");
        fixture.setAlignment(Qt::AlignCenter); fixture.resize(1000,180); fixture.move(30,30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(250);
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"); window.tessdata_=qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=fixture.screen(); window.selectedGeometry_=fixture.screen()->geometry();
        window.selectedDpr_=fixture.screen()->devicePixelRatio();
        window.region_=QRect(fixture.mapToGlobal(QPoint(0,0))-fixture.screen()->geometry().topLeft(),fixture.size());
        window.inlineCaptions_->setChecked(true);
        window.previewSubtitle();
        QVERIFY(window.subtitleOverlay_->isVisible());
        // Place the preview over the source on purpose: capture must hide it before OCR.
        window.subtitleOverlay_->move(fixture.mapToGlobal(QPoint(0,0)));
        QVERIFY(window.registered_);
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),WM_HOTKEY,1,0));
        QTRY_VERIFY_WITH_TIMEOUT(!window.original_->toPlainText().isEmpty(),25000);
        QVERIFY(window.original_->toPlainText().contains(QStringLiteral("天気")));
        QVERIFY(!window.busy_);
        QCOMPARE(window.lastSource_,int(ST_SCREEN_CAPTURE));
        QVERIFY(window.subtitleOverlay_->isVisible());
        QVERIFY(window.result_->toPlainText().isEmpty()); // Missing key is not a translation.
        QVERIFY(!window.resultWindow_->isVisible());
        QVERIFY(window.subtitleOverlay_->geometry().top() > fixture.mapToGlobal(QPoint(0, fixture.height())).y());
        window.inlineCaptions_->setChecked(false);
        QVERIFY(!window.subtitleOverlay_->isVisible());
        window.showResult(); window.resultWindow_->move(200,180);
        QTest::qWait(50); QCOMPARE(window.resultWindow_->pos(),QPoint(200,180));
    }
    void mouseSizeIsRememberedAndInvalidSizeUsesDefault() {
        {
            MainWindow window;
            QCOMPARE(window.cursorSizeBox_->currentData().toSize(), QSize(640, 160));
            window.cursorSizeBox_->setCurrentIndex(2);
        }
        {
            MainWindow next;
            QCOMPARE(next.cursorSizeBox_->currentData().toSize(), QSize(960, 240));
        }
        QSettings().setValue("cursor/size", QSize(-10, 999999));
        MainWindow invalid;
        QCOMPARE(invalid.cursorSizeBox_->currentData().toSize(), QSize(640, 160));
    }
    void actualCursorCaptureKeepsSavedRegionAndRetryAnchor() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        const QPoint oldCursor = QCursor::pos();
        const auto restoreCursor = qScopeGuard([oldCursor] { QCursor::setPos(oldCursor); });
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:32px;");
        fixture.setAlignment(Qt::AlignCenter);
        fixture.resize(840,220); fixture.move(30,30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(200);
        MainWindow window;
        window.executable_.clear(); // Require real PaddleOCR, no fallback and no network.
        window.paddleDir_=qEnvironmentVariable("SUBTITLE_TEST_PADDLE");
        window.paddleEngine_=true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=fixture.screen();
        window.selectedGeometry_=fixture.screen()->geometry();
        window.selectedDpr_=fixture.screen()->devicePixelRatio();
        const QRect savedRegion(80, 400, 600, 50);
        window.region_=savedRegion; window.saveRegion();
        const QPoint point=fixture.mapToGlobal(fixture.rect().center());
        const QRect expected=Capture::aroundPoint(fixture.screen()->geometry(),point,QSize(640,160));
        window.setAutoTranslate(true);
        QVERIFY(window.autoTranslating());
        QVERIFY(window.cursorRegistered_);
        MSG mouseHotkey{}; mouseHotkey.message=WM_HOTKEY; mouseHotkey.wParam=3;
        qintptr handled=0;
        // Dispatch immediately after moving the real cursor; a queued message races with
        // the user moving their mouse while the test waits for the event loop.
        QCursor::setPos(point);
        QVERIFY(window.nativeEvent({},&mouseHotkey,&handled));
        QVERIFY(window.busy_);
        QCursor::setPos(point + QPoint(300,200)); // Source stays at the point sampled at the hotkey.
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,20000);
        QVERIFY2(window.original_->toPlainText().contains(QStringLiteral("天気")), qPrintable(window.original_->toPlainText()));
        QCOMPARE(window.ocr_.engineUsed(),QStringLiteral("PaddleOCR"));
        QVERIFY(!window.autoTranslating());
        QCOMPARE(window.captionArea_.rect,expected);
        QCOMPARE(window.lastCaptionArea_.rect,expected);
        QCOMPARE(window.lastSource_,int(ST_SCREEN_CAPTURE));
        QCOMPARE(window.region_,savedRegion);
        QCOMPARE(QSettings().value("capture/region").toRect(),savedRegion);
        const qreal dpr=fixture.screen()->devicePixelRatio();
        QCOMPARE(window.lastImage_.size(),QSize(qRound(expected.width()*dpr),qRound(expected.height()*dpr)));
        QVERIFY(window.result_->toPlainText().isEmpty()); // No key: OCR success is not translation success.
        QVERIFY(window.resultStatus_->text().contains(QStringLiteral("OCR 완료")));
        QVERIFY(window.subtitleOverlay_->isVisible());
        QVERIFY(!window.subtitleOverlay_->geometry().intersects(expected.translated(window.selectedGeometry_.topLeft())));
        window.previewSubtitle(); // Previewing the saved game region must not change retry's mouse anchor.
        QCOMPARE(window.captionArea_.rect,expected);
        window.retry_->click();
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,20000);
        QVERIFY(window.original_->toPlainText().contains(QStringLiteral("天気")));
        QCOMPARE(window.captionArea_.rect,expected);
        QCOMPARE(window.region_,savedRegion);
        const QString evidence=qEnvironmentVariable("SUBTITLE_TEST_EVIDENCE");
        if (!evidence.isEmpty()) {
            window.showNormal();
            QTest::qWait(100);
            QVERIFY(window.grab().save(QDir(evidence).filePath("windows-mouse-ui.png")));
        }
    }
    void cursorCaptureWorksWithoutSelectingGameRegion() {
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        if (!window.ocrReady()) QSKIP("Actual engine not configured");
        QVERIFY(window.region_.isEmpty());
        window.startCursorCapture();
        QVERIFY(window.busy_);
        QVERIFY(!window.captionArea_.rect.isEmpty());
        QVERIFY(window.region_.isEmpty());
        window.cancel(); // Cancel before the deferred capture reads the screen.
        QTest::qWait(300);
        QVERIFY(!window.busy_);
        QVERIFY(window.lastImage_.isNull());
        QVERIFY(!window.subtitleOverlay_->isVisible());
        QCOMPARE(st_state(window.session_),int(ST_CANCELLED));
    }
    void cancelledMouseCountdownNeverCaptures() {
        MainWindow window;
        window.scheduleCursorCapture();
        QVERIFY(window.busy_);
        QVERIFY(window.cancel_->isEnabled());
        QVERIFY(!window.cursorSizeBox_->isEnabled());
        window.setAutoTranslate(true);
        QVERIFY(!window.autoTranslating());
        window.cancel();
        QTest::qWait(3200);
        QVERIFY(!window.busy_);
        QVERIFY(window.lastImage_.isNull());
        QVERIFY(window.cursorSizeBox_->isEnabled());
        QCOMPARE(st_state(window.session_),int(ST_CANCELLED));
    }
    void hoverReadsOncePerStopAndCancelsWhenMoved() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:32px;");
        fixture.setAlignment(Qt::AlignCenter); fixture.resize(840,220); fixture.move(30,30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(150);
        MainWindow window;
        window.executable_.clear();
        window.paddleDir_=qEnvironmentVariable("SUBTITLE_TEST_PADDLE");
        window.paddleEngine_=true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_.clear(); window.consent_=false;
        QVERIFY(!window.hoverMode_->isChecked()); // No automatic capture on launch.
        QVERIFY(window.hoverRegistered_);
        MSG hotkey{}; hotkey.message=WM_HOTKEY; hotkey.wParam=4;
        qintptr handled=0;
        QVERIFY(window.nativeEvent({},&hotkey,&handled));
        QVERIFY(window.hoverMode_->isChecked());
        window.hoverTimer_->stop(); // Feed deterministic pointer/time samples; capture/OCR remain real.
        const QPoint point=fixture.mapToGlobal(fixture.rect().center());
        window.updateHover(point,0);
        window.updateHover(point+QPoint(3,2),450); // Small hand tremor does not restart the dwell.
        window.updateHover(point,999);
        QVERIFY(!window.busy_);
        QVERIFY(window.lastImage_.isNull());
        window.updateHover(point,1000);
        QVERIFY(window.busy_);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,20000);
        QVERIFY2(window.original_->toPlainText().contains(QStringLiteral("天気")),qPrintable(window.original_->toPlainText()));
        QCOMPARE(window.ocr_.engineUsed(),QStringLiteral("PaddleOCR"));
        QVERIFY(window.result_->toPlainText().isEmpty()); // No key: never fake a translation.
        QVERIFY(window.region_.isEmpty()); // No saved game region needed or overwritten.
        const auto firstRequest=window.request_;
        window.updateHover(point,3000); window.updateHover(point,7000);
        QCOMPARE(window.request_,firstRequest);
        QVERIFY(!window.busy_);

        const QPoint second=point+QPoint(30,0);
        window.updateHover(second,8000);
        window.updateHover(second,9000);
        QVERIFY(window.busy_); // A new location allows one more capture.
        window.updateHover(point,9001); // Move before the deferred capture reads pixels.
        QVERIFY(!window.busy_);
        QTest::qWait(250);
        QCOMPARE(window.request_,firstRequest);
        QVERIFY(!window.subtitleOverlay_->isVisible());

        window.updateHover(point,10001);
        QVERIFY(window.busy_);
        QVERIFY(window.nativeEvent({},&hotkey,&handled)); // Same key stops and cancels pending work.
        QVERIFY(!window.hoverMode_->isChecked());
        QVERIFY(!window.busy_);
        QTest::qWait(250);
        QCOMPARE(window.request_,firstRequest);
    }
    void hoverWaitsAfterDragAndDoesNotReadOwnWindow() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        window.apiKey_.clear(); window.consent_=false;
        window.setHoverTranslate(true); window.hoverTimer_->stop();
        window.showNormal(); window.move(20,20);
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QPoint own=window.mapToGlobal(window.rect().center());
        window.updateHover(own,0); window.updateHover(own,5000);
        QVERIFY(window.lastImage_.isNull()); QVERIFY(!window.busy_);
        window.hide();
        const QPoint point=QGuiApplication::primaryScreen()->geometry().center();
        window.updateHover(point,6000,true); // Held mouse button: do not capture.
        window.updateHover(point,12000,true);
        QVERIFY(!window.busy_);
        window.updateHover(point,12001);
        window.updateHover(point,13000);
        QVERIFY(!window.busy_);
        window.updateHover(point,13001);
        QVERIFY(window.busy_);
        window.cancel_->click(); // The cancel button also turns off hover mode.
        QVERIFY(!window.hoverMode_->isChecked());
        QVERIFY(!window.busy_);
        QTest::qWait(250);
        QVERIFY(window.lastImage_.isNull());
        window.updateHover(point,20000);
        QVERIFY(!window.busy_);
    }
    void hoverAndGameAutoAreExclusiveAndHoverIsNotSaved() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        {
            MainWindow window;
            window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
            window.selectedScreen_=QGuiApplication::primaryScreen();
            window.selectedGeometry_=window.selectedScreen_->geometry();
            window.selectedDpr_=window.selectedScreen_->devicePixelRatio();
            const QRect saved(80,100,600,50);
            window.region_=saved; window.saveRegion();
            window.setAutoTranslate(true);
            QVERIFY(window.autoTranslating());
            window.setHoverTranslate(true);
            QVERIFY(window.hoverMode_->isChecked());
            QVERIFY(!window.autoTranslating());
            QCOMPARE(window.region_,saved);
            window.setAutoTranslate(true);
            QVERIFY(!window.hoverMode_->isChecked());
            QVERIFY(window.autoTranslating());
            window.setAutoTranslate(false);
            window.setHoverTranslate(true);
            QVERIFY(window.hoverMode_->isChecked());
        }
        MainWindow next;
        QVERIFY(!next.hoverMode_->isChecked());
        QVERIFY(!next.hoverTimer_->isActive());
    }
    void overlayStaysOutsideSourceAndOnItsMonitor() {
        const QRect monitor(-1920, 0, 1920, 1040);
        const QRect source(-1800, 400, 600, 40);
        const QRect below = SubtitleOverlay::placement(source, monitor, QSize(600, 70));
        QVERIFY(monitor.contains(below));
        QVERIFY(below.top() > source.bottom());
        QVERIFY(!below.intersects(source));
        QCOMPARE(below.left(), source.left()); // Starts where the original line starts.
        const QRect bottomSource(-400, 990, 380, 40);
        const QRect above = SubtitleOverlay::placement(bottomSource, monitor, QSize(600, 70));
        QVERIFY(monitor.contains(above));
        QVERIFY(above.bottom() < bottomSource.top());
        QVERIFY(SubtitleOverlay::placement(monitor, monitor, QSize(600,70)).isEmpty());
    }
    void overlayHasTransparentBackgroundAndPreservesFocus() {
        QWidget backdrop;
        backdrop.setWindowTitle(QStringLiteral("자막 위치 검증 · 실제 번역 아님"));
        backdrop.setStyleSheet("background:#527767;");
        backdrop.resize(1000,380); backdrop.move(60,60);
        auto* text = new QLabel(QStringLiteral("今日はいい天気です。"), &backdrop);
        text->setStyleSheet("color:white;font-family:'Yu Gothic';font-size:32px;");
        text->setAlignment(Qt::AlignCenter); text->setGeometry(120,100,760,50);
        backdrop.show(); backdrop.activateWindow();
        QTest::qWait(150);
        const HWND focused = GetForegroundWindow();
        SubtitleOverlay overlay;
        const QRect source(text->mapToGlobal(QPoint()),text->size());
        QVERIFY(overlay.showText(QStringLiteral("번역 위치 미리보기 · 실제 번역 아님"), source,
                                 backdrop.screen()->availableGeometry()));
        QTest::qWait(100);
        QCOMPARE(GetForegroundWindow(), focused);
        const LONG_PTR style=GetWindowLongPtrW(reinterpret_cast<HWND>(overlay.winId()),GWL_EXSTYLE);
        QVERIFY(style & WS_EX_TRANSPARENT);
        QVERIFY(style & WS_EX_NOACTIVATE);
        QVERIFY(style & WS_EX_LAYERED);
        const QImage pixels=overlay.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(qAlpha(pixels.pixel(0,0)),0);
        int painted=0;
        for(int y=0;y<pixels.height();++y)
            for(int x=0;x<pixels.width();++x) painted += qAlpha(pixels.pixel(x,y))!=0;
        QVERIFY(painted>0);
        QVERIFY(painted < pixels.width()*pixels.height()/2); // Text only, no opaque panel.
        const QString evidence=qEnvironmentVariable("SUBTITLE_TEST_EVIDENCE");
        if(!evidence.isEmpty()) {
            QVERIFY(pixels.save(QDir(evidence).filePath("windows-transparent-caption.png")));
        }
    }
    void cancelledCaptureDoesNotResurrectCaption() {
        MainWindow window;
        window.selectedScreen_=QGuiApplication::primaryScreen();
        window.selectedGeometry_=window.selectedScreen_->geometry();
        window.selectedDpr_=window.selectedScreen_->devicePixelRatio();
        window.region_=QRect(80,100,600,50);
        window.previewSubtitle();
        QVERIFY(window.subtitleOverlay_->isVisible());
        window.setBusy(true); window.cancel();
        QVERIFY(!window.subtitleOverlay_->isVisible());
    }
    void autoTranslateReadsEachNewLineOnce() {
        if(qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:52px;");
        fixture.setAlignment(Qt::AlignCenter); fixture.resize(1000,180); fixture.move(30,30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(250);
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"); window.tessdata_=qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=fixture.screen(); window.selectedGeometry_=fixture.screen()->geometry();
        window.selectedDpr_=fixture.screen()->devicePixelRatio();
        window.region_=QRect(fixture.mapToGlobal(QPoint(0,0))-fixture.screen()->geometry().topLeft(),fixture.size());
        window.inlineCaptions_->setChecked(true);
        QVERIFY(window.autoRegistered_);
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),WM_HOTKEY,2,0));
        QTRY_VERIFY(window.autoTranslating());
        QVERIFY(window.auto_->isChecked());
        QTRY_VERIFY_WITH_TIMEOUT(window.original_->toPlainText().contains(QStringLiteral("天気")) && !window.busy_,25000);
        const auto firstRequest=window.request_;
        QTest::qWait(1600); // The unchanged line must not be read again.
        QCOMPARE(window.request_,firstRequest);
        fixture.setText(QStringLiteral("明日は雨が降ります。"));
        QTRY_VERIFY_WITH_TIMEOUT(window.request_!=firstRequest && !window.busy_ && !window.original_->toPlainText().isEmpty(),25000);
        QVERIFY(!window.original_->toPlainText().contains(QStringLiteral("天気")));
        QVERIFY(window.result_->toPlainText().isEmpty()); // Missing key is not a translation.
        QVERIFY(window.subtitleOverlay_->isVisible());
        QVERIFY(!window.subtitleOverlay_->geometry().intersects(QRect(fixture.mapToGlobal(QPoint(0,0)),fixture.size())));
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),WM_HOTKEY,2,0));
        QTRY_VERIFY(!window.autoTranslating());
        QVERIFY(!window.auto_->isChecked());
        QVERIFY(!window.subtitleOverlay_->isVisible());
    }
    void autoRetriesSameFrameAfterActualOcrFailure() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        QTemporaryDir brokenModels;
        QVERIFY(brokenModels.isValid());
        QFile broken(QDir(brokenModels.path()).filePath(QLatin1String(PaddleOcr::CjkModel)));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("invalid model for failure-path test"); broken.close();
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:42px;");
        fixture.setAlignment(Qt::AlignCenter);
        fixture.resize(840,160); fixture.move(30,30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(150);
        MainWindow window;
        window.executable_.clear();
        window.paddleDir_=brokenModels.path(); window.paddleEngine_=true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=fixture.screen(); window.selectedGeometry_=fixture.screen()->geometry();
        window.selectedDpr_=fixture.screen()->devicePixelRatio();
        window.region_=QRect(fixture.mapToGlobal(QPoint())-fixture.screen()->geometry().topLeft(),fixture.size());
        window.setAutoTranslate(true);
        QTRY_VERIFY_WITH_TIMEOUT(st_state(window.session_)==ST_FAILED && !window.busy_,15000);
        QVERIFY(window.status_->text().contains(QStringLiteral("3초")));
        const auto failedRequest=window.request_;
        // Restore the real model while keeping every source pixel unchanged.
        window.paddleDir_=qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.applyOcrEngine();
        QTRY_VERIFY_WITH_TIMEOUT(window.request_!=failedRequest && !window.busy_ &&
            window.original_->toPlainText().contains(QStringLiteral("天気")),20000);
        QVERIFY(window.result_->toPlainText().isEmpty()); // No live translation request.
        QVERIFY(window.autoTranslating());
        const auto recoveredRequest=window.request_;
        QTest::qWait(3600);
        QCOMPARE(window.request_,recoveredRequest); // A successful static frame stops retrying.
        window.setAutoTranslate(false);
    }
    void regionIsRememberedForTheNextLaunch() {
        const QRect region(80,100,600,50);
        {
            MainWindow window;
            window.selectedScreen_=QGuiApplication::primaryScreen();
            window.selectedGeometry_=window.selectedScreen_->geometry();
            window.selectedDpr_=window.selectedScreen_->devicePixelRatio();
            window.region_=region;
            window.saveRegion();
        }
        MainWindow next;
        QCOMPARE(next.region_,region);
        QCOMPARE(next.selectedScreen_.data(),QGuiApplication::primaryScreen());
        QVERIFY(next.regionLabel_->text().contains(QStringLiteral("저장된 영역")));
        QSettings().remove("capture");
        MainWindow fresh;
        QVERIFY(fresh.region_.isEmpty());
    }
    void koreanLineIsNotSentWhenTranslatingIntoKorean() {
        if(qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"); window.tessdata_=qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        window.apiKey_=QStringLiteral("fixture-key:fx"); window.consent_=true; // must stay unused: nothing is foreign
        window.targetLanguageBox_->setCurrentIndex(window.targetLanguageBox_->findData(QStringLiteral("KO")));
        QImage image(1000,120,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter p(&image); QFont f("Malgun Gothic"); f.setPixelSize(48); p.setFont(f); p.setPen(Qt::black);
        p.drawText(image.rect(),Qt::AlignCenter,QStringLiteral("오늘은 날씨가 좋습니다.")); p.end();
        window.processImage(image,ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,25000);
        QVERIFY(!window.translation_.isBusy());
        QVERIFY2(window.resultStatus_->text().contains(QStringLiteral("이미")), qPrintable(window.resultStatus_->text()));
        QVERIFY(window.result_->toPlainText().isEmpty());
        QCOMPARE(st_state(window.session_),int(ST_COMPLETE));
    }
    void paddleReadsGameTextWithoutTesseract() {
        if(qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        MainWindow window;
        window.executable_.clear(); // no fallback: PaddleOCR alone must read it
        window.paddleDir_=qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_=true; window.applyOcrEngine();
        window.apiKey_.clear(); window.consent_=false;
        QVERIFY(window.ocrReady());
        window.processImage(QImage(QDir(QStringLiteral(OCR_FIXTURE_DIR)).filePath("outlined-ko-18px.png")),ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,20000);
        QVERIFY2(window.original_->toPlainText().contains(QStringLiteral("날씨")), window.original_->toPlainText().toUtf8().constData());
        QCOMPARE(window.ocr_.engineUsed(),QStringLiteral("PaddleOCR"));
        window.paddleEngine_=false; window.applyOcrEngine();
        QVERIFY(!window.ocrReady()); // Tesseract-only with no tesseract.exe: nothing can read
    }
    void foreignLatinIsNotSkippedForEnglish() {
        if(qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        MainWindow window;
        window.paddleDir_=qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_=true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData(QStringLiteral("fra")));
        window.targetLanguageBox_->setCurrentIndex(window.targetLanguageBox_->findData(QStringLiteral("EN-US")));
        window.apiKey_.clear(); window.consent_=false;
        window.processImage(QImage(QDir(QStringLiteral(OCR_FIXTURE_DIR)).filePath("multilang/fr-0-24-dark-on-light-tight.png")),ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,20000);
        QVERIFY(window.original_->toPlainText().startsWith(QStringLiteral("Voulez")));
        QVERIFY(!window.segments_.empty());
        QVERIFY(window.segments_.front().translate);
        QVERIFY(!window.resultStatus_->text().contains(QStringLiteral("이미")));
        QVERIFY(window.resultStatus_->text().contains(QStringLiteral("OCR 완료")));
        QVERIFY(!window.translation_.isBusy()); // no key or consent; no live API request
    }
    void autoWithoutRegionWaitsForSelection() {
        MainWindow window;
        window.setAutoTranslate(true);
        QVERIFY(!window.autoTranslating());
        QVERIFY(window.autoPending_);
        QVERIFY(!window.auto_->isChecked());
    }
    void cancelInvalidatesPendingResult() {
        MainWindow window; window.setBusy(true);
        window.cancel(); QVERIFY(!window.busy_); QCOMPARE(st_state(window.session_),int(ST_CANCELLED));
    }
};
QTEST_MAIN(WindowChecks)
#include "window_checks.moc"
