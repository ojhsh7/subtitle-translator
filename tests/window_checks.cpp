#include "main_window.h"
#include "region_selector.h"
#include "subtitle_overlay.h"
#include <QCheckBox>
#include <QSettings>
#include <QDir>
#include <QtTest>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QPainter>
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
    void overlayStaysOutsideSourceAndOnItsMonitor() {
        const QRect monitor(-1920, 0, 1920, 1040);
        const QRect source(-1800, 400, 600, 40);
        const QRect below = SubtitleOverlay::placement(source, monitor, QSize(600, 70));
        QVERIFY(monitor.contains(below));
        QVERIFY(below.top() > source.bottom());
        QVERIFY(!below.intersects(source));
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
    void cancelInvalidatesPendingResult() {
        MainWindow window; window.setBusy(true);
        window.cancel(); QVERIFY(!window.busy_); QCOMPARE(st_state(window.session_),int(ST_CANCELLED));
    }
};
QTEST_MAIN(WindowChecks)
#include "window_checks.moc"
