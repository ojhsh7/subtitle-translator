#include "main_window.h"
#include "capture_service.h"
#include "game_window.h"
#include "local_http.h"
#include "paddle_ocr.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cstdlib>
#include <QCursor>
#include <QScopeGuard>
#include "region_selector.h"
#include "subtitle_overlay.h"
#include "fluent_widgets.h"
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
#include <QAction>
#include <QElapsedTimer>
#include <QFontInfo>
#include <QFontMetrics>
#include <QStyleHints>
#include <QDialog>
#include <QKeySequence>
#include <QListWidget>
#include <QSystemTrayIcon>
#include <windows.h>
namespace {
// Logical -> physical -> logical conversion may round by a pixel on scaled monitors.
bool closeTo(const QRect& a, const QRect& b, int tolerance = 2) {
    return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance &&
           std::abs(a.width() - b.width()) <= tolerance && std::abs(a.height() - b.height()) <= tolerance;
}
QString rectText(const QRect& r) { return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); }
}
class WindowChecks : public QObject {
    Q_OBJECT
private slots:

    // P-31 L2: which reads count as finished sentences (others wait for SentenceSettleMs of stillness).
    void sentenceEndsAreRecognized() {
        for (const char* finished : {"今日はいい天気です。", "「行こう！」", "Really?", "えっと…", "Wait.", "やった♪"})
            QVERIFY2(MainWindow::looksFinished(QString::fromUtf8(finished)), finished);
        for (const char* unfinished : {"今日は、", "今日はいい天気", "Hello, my", "そして"})
            QVERIFY2(!MainWindow::looksFinished(QString::fromUtf8(unfinished)), unfinished);
    }
    // P-33 G1/G2: shown lines go to the backlog (memory only); fixes change the original before it is
    // sent and the translation before it is shown.
    void backlogAndTextFixes() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        auto line = [](const QString& text) {
            QImage image(1000, 140, QImage::Format_RGB32); image.fill(Qt::white);
            QPainter p(&image); QFont f("Yu Gothic"); f.setPixelSize(52); p.setFont(f); p.setPen(Qt::black);
            p.drawText(image.rect(), Qt::AlignCenter, text); p.end();
            return image;
        };
        {
            QSettings saved;
            saved.beginWriteArray("fixes");
            saved.setArrayIndex(0);
            saved.setValue("onTranslation", true); saved.setValue("find", QStringLiteral("유우키")); saved.setValue("replace", QStringLiteral("유키"));
            saved.setArrayIndex(1);
            saved.setValue("onTranslation", false); saved.setValue("find", QStringLiteral("明日")); saved.setValue("replace", QStringLiteral("あした"));
            saved.endArray();
        }
        auto cleanup = qScopeGuard([] { QSettings().remove("fixes"); });
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"유우키, 오늘은 날씨가 좋아요.\"}]}"}
                     << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"내일은 비가 와요.\"}]}"};
        MainWindow window;
        QCOMPARE(int(window.fixes_.size()), 2);
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_ = QStringLiteral("test-key:fx"); window.consent_ = true; window.freeApi_ = true;
        window.translation_.setEndpointForTests(http.url());
        window.processImage(line(QStringLiteral("今日はいい天気です。")), ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QVERIFY2(window.result_->toPlainText().startsWith(QStringLiteral("유키,")), qPrintable(window.result_->toPlainText()));
        window.processImage(line(QStringLiteral("明日は雨が降ります。")), ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QCOMPARE(int(http.requests.size()), 2);
        QVERIFY2(http.requests[1].contains(QStringLiteral("あした").toUtf8()), http.requests[1].constData()); // fixed before sending
        QVERIFY(!http.requests[1].contains(QStringLiteral("明日").toUtf8()));
        // P-35: the second request carries the first line as context; the first had none.
        QVERIFY(!http.requests[0].contains("\"context\""));
        QVERIFY2(http.requests[1].contains("\"context\"") && http.requests[1].contains(QStringLiteral("今日").toUtf8()),
                 http.requests[1].constData());
        QCOMPARE(int(window.backlog_.size()), 2);
        QCOMPARE(window.backlogList_->count(), 2);
        QVERIFY(window.backlog_.first().translation.startsWith(QStringLiteral("유키,")));
        QVERIFY(window.backlog_.last().source.contains(QStringLiteral("あした")));
        window.addToBacklog(window.backlog_.last().source, window.backlog_.last().translation); // the same line read again
        QCOMPARE(int(window.backlog_.size()), 2);
        window.resultWindow_->hide();
    }
    // P-24: a sentence typed for longer than MaxWaitMs is read once while still typing. That read does not
    // end a sentence, so it waits, and it is dropped as the text keeps changing: only the finished
    // sentence is sent.
    void longTypewriterSentenceIsSentOnce() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        const QString full = QStringLiteral("今日はいい天気ですね明日も晴れるといいですね本当にそう思います。");
        QLabel fixture(full.left(1));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:36px;");
        fixture.setAlignment(Qt::AlignLeft | Qt::AlignVCenter); fixture.resize(1300, 110); fixture.move(30, 30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        int shown = 1;
        QTimer typing; typing.setInterval(120); // 32 characters: about 3.8 s, longer than MaxWaitMs
        connect(&typing, &QTimer::timeout, &fixture, [&] { if (shown < full.size()) fixture.setText(full.left(++shown)); });
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"오늘은 날씨가 좋네요.\"}]}"};
        MainWindow window;
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_ = QStringLiteral("test-key:fx"); window.consent_ = true; window.freeApi_ = true;
        window.translation_.setEndpointForTests(http.url());
        window.followGame_->setChecked(false);
        window.setRegion(fixture.screen(), QRect(fixture.mapToGlobal(QPoint()) - fixture.screen()->geometry().topLeft(), fixture.size()));
        window.setAutoTranslate(true);
        typing.start();
        QTRY_VERIFY_WITH_TIMEOUT(shown == full.size(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(int(http.requests.size()), 1, 10000);
        QTest::qWait(1500);
        QCOMPARE(int(http.requests.size()), 1); // the partial read while typing was never sent
        QVERIFY2(http.requests[0].contains(QStringLiteral("思います").toUtf8()), http.requests[0].constData());
        window.setAutoTranslate(false);
    }
    // P-36: a wrong key is told right after the start or a save, before a line is lost.
    void wrongKeyIsToldAtOnce() {
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{403, "{\"message\":\"Wrong key\"}"};
        MainWindow window;
        window.apiKey_ = QStringLiteral("wrong-key:fx"); window.consent_ = true;
        window.usage_.setEndpointForTests(http.url());
        window.refreshUsage();
        QTRY_VERIFY_WITH_TIMEOUT(window.status_->text().contains(QStringLiteral("키")), 5000);
        QVERIFY2(window.status_->text().contains(QStringLiteral("요금제")), qPrintable(window.status_->text()));
    }
    // P-36: captions can be held back while reading and translating go on; the tray item follows.
    void captionsCanBeHidden() {
        MainWindow window;
        window.setRegion(QGuiApplication::primaryScreen(), QRect(200, 300, 600, 50));
        window.game_ = {};
        window.inlineCaptions_->setChecked(true);
        window.activeSource_ = ST_SCREEN_CAPTURE; window.lastSource_ = ST_SCREEN_CAPTURE;
        window.captionArea_ = window.selectedArea();
        window.result_->setPlainText(QStringLiteral("숨김 확인용 자막"));
        QVERIFY(window.showSubtitle(window.result_->toPlainText()));
        QVERIFY(window.subtitleOverlay_->isVisible());
        window.setCaptionsHidden(true);
        QVERIFY(!window.subtitleOverlay_->isVisible());
        QVERIFY(window.showSubtitle(QStringLiteral("다음 자막"))); // handled: no result window instead
        QVERIFY(!window.subtitleOverlay_->isVisible());
        QVERIFY(!window.resultWindow_->isVisible());
        if (window.trayCaptions_) QVERIFY(window.trayCaptions_->isChecked());
        window.setCaptionsHidden(false);
        QVERIFY(window.subtitleOverlay_->isVisible());
        window.subtitleOverlay_->hide();
    }
    // P-36: every icon the app draws exists in the icon font in use (Windows 11 or the Windows 10 one).
    void iconsExistInTheIconFont() {
        const QFontMetrics metrics(Look::iconFont(16));
        for (const char16_t glyph : {Look::Settings, Look::SelectArea, Look::Translate, Look::Mouse, Look::More, Look::Captions,
                                     Look::Keyboard, Look::Globe, Look::Scan, Look::Camera, Look::Info, Look::System,
                                     Look::Forward, Look::Document, Look::Edit})
            QVERIFY2(metrics.inFontUcs4(glyph), qPrintable(QString::number(int(glyph), 16)));
        QFont mdl2(QStringLiteral("Segoe MDL2 Assets")); mdl2.setPixelSize(16);
        if (QFontInfo(mdl2).family() == QStringLiteral("Segoe MDL2 Assets")) // the Windows 10 fallback, when installed
            for (const char16_t glyph : {Look::SelectArea, Look::Translate, Look::Mouse, Look::Captions, Look::Scan, Look::Edit})
                QVERIFY(QFontMetrics(mdl2).inFontUcs4(glyph));
    }
    // P-36: dark mode follows Windows; text stays readable. SUBTITLE_TEST_SCREENSHOTS saves the screens.
    void darkModeFollowsWindows() {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        auto restore = qScopeGuard([] { QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Unknown); });
        QTest::qWait(100);
        MainWindow window;
        QVERIFY(window.palette().color(QPalette::Window).lightnessF() < 0.5);
        QVERIFY(window.status_->palette().color(QPalette::WindowText).lightnessF() > 0.5);
        QVERIFY(window.regionLabel_->palette().color(QPalette::WindowText).lightnessF() > 0.4); // secondary text, still light
        const QString shots = qEnvironmentVariable("SUBTITLE_TEST_SCREENSHOTS");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (!shots.isEmpty()) {
            window.grab().save(QDir(shots).filePath("dark-main.png"));
            window.reloadSettings_();
            window.settingsPages_->setCurrentRow(1);
            window.settingsWindow_->show();
            QVERIFY(QTest::qWaitForWindowExposed(window.settingsWindow_));
            QTest::qWait(50);
            window.settingsWindow_->grab().save(QDir(shots).filePath("dark-settings-1.png"));
            window.settingsWindow_->hide();
        }
        window.hide();
    }
    // P-16: the problem report has states and times, never the text read or translated.
    void problemReportHoldsNoText() {
        MainWindow window;
        window.original_->setPlainText(QStringLiteral("비밀 원문 문장"));
        window.result_->setPlainText(QStringLiteral("비밀 번역 문장"));
        const QString report = window.diagnostics();
        QVERIFY2(report.contains(QStringLiteral("OCR 스레드")), qPrintable(report));
        QVERIFY(report.contains(QStringLiteral("모니터")));
        QVERIFY(!report.contains(QStringLiteral("비밀")));
    }
    // P-33 G3: each game keeps the source language chosen while it was in front.
    void eachGameKeepsItsLanguage() {
        MainWindow window;
        const int japanese = window.ocrLanguageBox_->findData("jpn"), english = window.ocrLanguageBox_->findData("eng"),
                  korean = window.ocrLanguageBox_->findData("kor");
        QVERIFY(japanese >= 0 && english >= 0 && korean >= 0);
        window.game_.key = QStringLiteral("C:/games/novel.exe");
        window.ocrLanguageBox_->setCurrentIndex(korean);
        window.ocrLanguageBox_->setCurrentIndex(japanese);
        window.game_.key = QStringLiteral("C:/games/rpg.exe");
        window.ocrLanguageBox_->setCurrentIndex(english);
        window.game_.key = QStringLiteral("C:/games/novel.exe");
        window.restoreGameLanguage();
        QCOMPARE(window.ocrLanguages(), QStringLiteral("jpn"));
        window.game_.key = QStringLiteral("C:/games/rpg.exe");
        window.restoreGameLanguage();
        QCOMPARE(window.ocrLanguages(), QStringLiteral("eng"));
        window.game_ = {};
    }
    // P-33 G4/G5: usage is shown and warns near the limit; the empty stage says what to do next.
    void usageAndNextStepGuidance() {
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"character_count\":460000,\"character_limit\":500000}"};
        MainWindow window;
        window.apiKey_.clear(); window.consent_ = false; window.region_ = {}; window.selectedScreen_ = nullptr; window.game_ = {};
        window.refreshUi();
        QVERIFY2(window.stage_->placeholder().contains(QStringLiteral("DeepL 키")), qPrintable(window.stage_->placeholder()));
        window.apiKey_ = QStringLiteral("test-key:fx"); window.consent_ = true;
        window.refreshUi();
        QVERIFY2(window.stage_->placeholder().contains(QStringLiteral("영역 선택")), qPrintable(window.stage_->placeholder()));
        window.setRegion(QGuiApplication::primaryScreen(), QRect(80, 100, 600, 50));
        window.game_ = {};
        window.refreshUi();
        QVERIFY2(window.stage_->placeholder().contains(QStringLiteral("자동 번역")), qPrintable(window.stage_->placeholder()));
        window.usage_.setEndpointForTests(http.url());
        window.refreshUsage();
        QTRY_VERIFY_WITH_TIMEOUT(window.usageLimit_ > 0, 5000);
        QVERIFY(window.usageAction_->isVisible());
        QVERIFY2(window.usageAction_->text().contains(QStringLiteral("92%")), qPrintable(window.usageAction_->text()));
        QVERIFY2(window.status_->text().contains(QStringLiteral("92%")), qPrintable(window.status_->text())); // past 90%: told
    }
    // P-31 L4: a line translated within the last CacheLines lines is shown from memory, with no request.
    void repeatedLineIsTranslatedFromMemory() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        auto line = [](const QString& text) {
            QImage image(1000, 140, QImage::Format_RGB32); image.fill(Qt::white);
            QPainter p(&image); QFont f("Yu Gothic"); f.setPixelSize(52); p.setFont(f); p.setPen(Qt::black);
            p.drawText(image.rect(), Qt::AlignCenter, text); p.end();
            return image;
        };
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"오늘은 날씨가 좋아요.\"}]}"}
                     << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"내일은 비가 와요.\"}]}"};
        MainWindow window;
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_ = QStringLiteral("test-key:fx"); window.consent_ = true; window.freeApi_ = true;
        window.translation_.setEndpointForTests(http.url());
        window.processImage(line(QStringLiteral("今日はいい天気です。")), ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QCOMPARE(int(http.requests.size()), 1);
        window.processImage(line(QStringLiteral("明日は雨が降ります。")), ST_IMAGE_FILE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QCOMPARE(int(http.requests.size()), 2);
        QElapsedTimer clock; clock.start();
        window.processImage(line(QStringLiteral("今日はいい天気です。")), ST_IMAGE_FILE); // not the last line: the core's one-line cache cannot answer
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        qInfo("repeated line shown %lld ms after the read began (OCR included, no request)", clock.elapsed());
        QCOMPARE(int(http.requests.size()), 2);
        QVERIFY2(window.result_->toPlainText().contains(QStringLiteral("오늘은")), qPrintable(window.result_->toPlainText()));
        QVERIFY2(window.resultStatus_->text().contains(QStringLiteral("기억해 둔")), qPrintable(window.resultStatus_->text()));
        window.resultWindow_->hide();
    }
    // P-31 L2: in auto mode a line without a sentence ending is read at once but sent only after the
    // frame stayed still for SentenceSettleMs, and only once.
    void unfinishedLineWaitsThenIsSentOnce() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        QLabel fixture(QStringLiteral("今日はいい天気"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:44px;");
        fixture.setAlignment(Qt::AlignCenter); fixture.resize(900, 160); fixture.move(30, 30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(200);
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"오늘은 좋은 날씨\"}]}"};
        MainWindow window;
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_ = QStringLiteral("test-key:fx"); window.consent_ = true; window.freeApi_ = true;
        window.translation_.setEndpointForTests(http.url());
        window.followGame_->setChecked(false);
        window.setRegion(fixture.screen(), QRect(fixture.mapToGlobal(QPoint()) - fixture.screen()->geometry().topLeft(), fixture.size()));
        QElapsedTimer clock; clock.start();
        window.setAutoTranslate(true);
        QTRY_VERIFY_WITH_TIMEOUT(window.holding_ || !http.requests.isEmpty(), 10000);
        QVERIFY(window.holding_);                        // read, not yet sent
        QVERIFY(http.requests.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(int(http.requests.size()), 1, 10000);
        const qint64 sent = clock.elapsed();
        QTRY_VERIFY_WITH_TIMEOUT(window.result_->toPlainText().contains(QStringLiteral("오늘은")), 10000);
        QTest::qWait(1000);
        QCOMPARE(int(http.requests.size()), 1);          // the same still line is not sent again
        qInfo("unfinished line sent %lld ms after auto translation started", sent);
        QVERIFY(sent >= ChangeDetector::SentenceSettleMs);
        window.setAutoTranslate(false);
    }
    // P-05: in auto mode, 456 (quota exhausted) stops auto translation; 429 (too many requests)
    // keeps it running but sends nothing until the server allows it. Local HTTP server, never DeepL.
    void autoTranslationHandlesRateLimitAndQuota() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual engine not configured");
        auto line = [](const QString& text) {
            QImage image(1000, 140, QImage::Format_RGB32); image.fill(Qt::white);
            QPainter p(&image); QFont f("Yu Gothic"); f.setPixelSize(52); p.setFont(f); p.setPen(Qt::black);
            p.drawText(image.rect(), Qt::AlignCenter, text); p.end();
            return image;
        };
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{429, "{}", "Retry-After: 2\r\n"} << LocalHttp::Reply{456, "{}"};
        MainWindow window;
        window.executable_ = qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"); window.tessdata_ = qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        window.apiKey_ = QStringLiteral("test-key:fx"); window.consent_ = true; window.freeApi_ = true;
        window.translation_.setEndpointForTests(http.url());
        window.selectedScreen_ = QGuiApplication::primaryScreen();
        window.selectedGeometry_ = window.selectedScreen_->geometry();
        window.selectedDpr_ = window.selectedScreen_->devicePixelRatio();
        window.region_ = QRect(80, 100, 600, 50);
        window.followGame_->setChecked(false);
        window.setAutoTranslate(true);
        window.autoTimer_->setInterval(60000); // lines are fed by hand below
        window.processImage(line(QStringLiteral("今日はいい天気です。")), ST_SCREEN_CAPTURE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QCOMPARE(int(http.requests.size()), 1);
        QVERIFY(window.autoTranslating());                       // 429 pauses, it does not stop
        QVERIFY2(window.status_->text().contains(QStringLiteral("10초")), qPrintable(window.status_->text())); // at least 10 s
        window.processImage(line(QStringLiteral("明日は雨が降ります。")), ST_SCREEN_CAPTURE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QCOMPARE(int(http.requests.size()), 1);                  // paused: no request for the next line
        QVERIFY(window.status_->text().contains(QStringLiteral("원문만")));
        QVERIFY(window.original_->toPlainText().contains(QStringLiteral("雨")));
        window.translationPausedUntil_ = -1;                     // the pause ends
        window.processImage(line(QStringLiteral("今日はいい天気です。")), ST_SCREEN_CAPTURE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QCOMPARE(int(http.requests.size()), 2);
        QVERIFY(!window.autoTranslating());                      // 456 stops auto translation
        QVERIFY2(window.status_->text().contains(QStringLiteral("한도")), qPrintable(window.status_->text()));
    }
    // F1: the region chosen over a game window follows it when the window moves, resizes or is minimized.
    void gameRegionFollowsMoveResizeAndMinimize() {
        QWidget game;
        game.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        game.setStyleSheet("background:#203040;");
        game.resize(800, 450); game.move(40, 40); game.show();
        QVERIFY(QTest::qWaitForWindowExposed(&game));
        QTest::qWait(200);
        MainWindow window;
        window.ignoreProcess_ = 0; // the test window plays the game
        QScreen* screen = game.screen();
        const QRect chosen(game.mapToGlobal(QPoint(100, 300)) - screen->geometry().topLeft(), QSize(500, 80));
        window.setRegion(screen, chosen);
        QCOMPARE(window.game_.key, GameWindow::normalizedKey(QCoreApplication::applicationFilePath()));
        QVERIFY(window.regionLabel_->text().contains(QStringLiteral("게임 창")));

        game.move(240, 140);
        QTRY_VERIFY2_WITH_TIMEOUT(window.followGame(false) && closeTo(window.region_, chosen.translated(200, 100)),
                                  qPrintable(rectText(window.region_)), 3000);

        game.resize(1200, 675); // 1.5x: games scale their dialogue box with the window
        const QRect scaled(game.mapToGlobal(QPoint(150, 450)) - screen->geometry().topLeft(), QSize(750, 120));
        QTRY_VERIFY2_WITH_TIMEOUT(window.followGame(false) && closeTo(window.region_, scaled),
                                  qPrintable(rectText(window.region_) + " vs " + rectText(scaled)), 3000);

        // In this test the app and the "game" are one program, so the app's own main window would match
        // the game's program path. Hide it; a real game is a different program (and `at` skips our windows).
        window.setAutoTranslate(true);
        window.autoTimer_->setInterval(60000); // ticks are driven by hand below
        QVERIFY(window.autoTranslating());
        window.hide();
        QTest::qWait(300);
        game.showMinimized();
        QTRY_VERIFY2_WITH_TIMEOUT(!window.followGame(false), qPrintable([&] {
            const auto seen = GameWindow::find(window.game_.key, 0);
            wchar_t cls[128]{}; GetClassNameW(reinterpret_cast<HWND>(seen.handle), cls, 128);
            return QStringLiteral("still found: '%1' class=%2 game=%3 main=%4 overlay=%5")
                .arg(seen.title, QString::fromWCharArray(cls)).arg(seen.handle).arg(quintptr(window.winId()))
                .arg(quintptr(window.subtitleOverlay_->winId()));
        }()), 3000);
        window.autoTick(); // must wait, not stop
        QVERIFY(window.autoTranslating());
        QVERIFY2(window.status_->text().contains(QStringLiteral("대기")), qPrintable(window.status_->text()));
        QVERIFY(!window.busy_);
        game.showNormal();
        QTRY_VERIFY_WITH_TIMEOUT(window.followGame(false), 3000);
        window.setAutoTranslate(false);
    }
    // F1 with the real engine: after the window moved, Ctrl+Alt+T still reads the same line.
    void gameRegionReadsTextAfterWindowMoves() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        QLabel game(QStringLiteral("今日はいい天気です。"));
        game.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        game.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:44px;");
        game.setAlignment(Qt::AlignCenter); game.resize(900, 160); game.move(30, 30); game.show();
        QVERIFY(QTest::qWaitForWindowExposed(&game));
        QTest::qWait(250);
        MainWindow window;
        window.ignoreProcess_ = 0;
        window.executable_.clear();
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_.clear(); window.consent_ = false;
        window.setRegion(game.screen(), QRect(game.mapToGlobal(QPoint()) - game.screen()->geometry().topLeft(), game.size()));
        QVERIFY(!window.game_.key.isEmpty());
        game.move(330, 260);
        QTest::qWait(250);
        QVERIFY(window.registered_);
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), WM_HOTKEY, 1, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_ && !window.original_->toPlainText().isEmpty(), 25000);
        QVERIFY2(window.original_->toPlainText().contains(QStringLiteral("天気")), qPrintable(window.original_->toPlainText()));
        QVERIFY(closeTo(window.region_.translated(window.selectedGeometry_.topLeft()), QRect(game.mapToGlobal(QPoint()), game.size())));
        QVERIFY(window.result_->toPlainText().isEmpty()); // no key: not a translation
    }
    // F2: every game keeps its region; the next launch restores it and the front game wins.
    void gameRegionIsRememberedAndFollowsTheFrontGame() {
        QWidget game;
        game.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        game.resize(700, 400); game.move(60, 60); game.show();
        QVERIFY(QTest::qWaitForWindowExposed(&game));
        QTest::qWait(200);
        QRect chosen;
        {
            MainWindow window;
            window.ignoreProcess_ = 0;
            chosen = QRect(game.mapToGlobal(QPoint(50, 280)) - game.screen()->geometry().topLeft(), QSize(400, 60));
            window.setRegion(game.screen(), chosen);
            QVERIFY(!window.game_.key.isEmpty());
        }
        game.move(160, 110);
        QTest::qWait(200);
        MainWindow next;
        next.ignoreProcess_ = 0;
        QVERIFY(next.knownGames_.contains(GameWindow::normalizedKey(QCoreApplication::applicationFilePath())));
        QVERIFY(next.hasRegion());
        QTRY_VERIFY_WITH_TIMEOUT(next.followGame(false) && closeTo(next.region_, chosen.translated(100, 50)), 3000);
        // A different saved game is current; when this game is in front, its own region is used again.
        // Windows refuses the foreground to a background test process, so the front window is supplied.
        const GameWindow::Info thisGame = GameWindow::at(game.mapToGlobal(QPoint(10, 10)), 0);
        QCOMPARE(thisGame.handle, quintptr(game.winId()));
        const GameWindow::Info browser{1, QStringLiteral("c:/apps/browser.exe"), QStringLiteral("browser.exe"), {}, QRect(0, 0, 10, 10)};
        next.game_ = {QStringLiteral("c:/games/other/other.exe"), QStringLiteral("other.exe"), QRect(0, 0, 100, 40), QSize(800, 600)};
        next.foregroundWindow_ = [&] { return browser; }; // an unknown program in front: keep the current game
        QVERIFY(!next.followGame(true));                    // other.exe is not running
        QCOMPARE(next.game_.key, QStringLiteral("c:/games/other/other.exe"));
        next.foregroundWindow_ = [&] { return thisGame; };
        QVERIFY(next.followGame(true));
        QCOMPARE(next.game_.key, GameWindow::normalizedKey(QCoreApplication::applicationFilePath()));
        QVERIFY(closeTo(next.region_, chosen.translated(100, 50)));
        QCOMPARE(QSettings().value("capture/game").toString(), next.game_.key); // remembered for the next launch
        // Turning tracking off keeps the plain screen region.
        next.followGame_->setChecked(false);
        QVERIFY(next.game_.key.isEmpty());
        QVERIFY(next.hasRegion());
    }
    // F3: the region hotkey opens the selector on the monitor under the mouse, also while minimized.
    void regionHotkeyOpensSelector() {
        MainWindow window;
        if (!window.regionRegistered_) QSKIP("Ctrl+Alt+R and Ctrl+Alt+F7 are both taken on this PC");
        window.showMinimized();
        // The selector runs its own event loop, so look for it from a timer inside that loop.
        bool opened = false;
        QRect geometry;
        QTimer probe;
        probe.setInterval(50);
        connect(&probe, &QTimer::timeout, &probe, [&] {
            if (auto* selector = dynamic_cast<RegionSelector*>(QApplication::activeModalWidget())) {
                opened = true; geometry = selector->geometry();
                QTest::keyClick(selector, Qt::Key_Escape);
            }
        });
        probe.start();
        const QRect expected = QGuiApplication::screenAt(QCursor::pos())->geometry();
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), WM_HOTKEY, 5, 0));
        QTRY_VERIFY_WITH_TIMEOUT(opened && !window.modal_, 5000);
        probe.stop();
        QCOMPARE(geometry, expected);
        QVERIFY(window.region_.isEmpty()); // cancelled: nothing chosen
    }
    // F4: keyboard adjustment of the current region; Enter confirms it, Esc cancels.
    void regionSelectorKeyboardAdjustsCurrentRegion() {
        QPixmap snapshot(400, 300); snapshot.fill(Qt::darkGray);
        const QRect screenRect(QGuiApplication::primaryScreen()->geometry().topLeft(), QSize(400, 300));
        RegionSelector selector(snapshot, screenRect, QRect(50, 60, 100, 40));
        selector.show();
        QVERIFY(QTest::qWaitForWindowExposed(&selector));
        for (int i = 0; i < 3; ++i) QTest::keyClick(&selector, Qt::Key_Right);
        QTest::keyClick(&selector, Qt::Key_Right, Qt::ShiftModifier);  // +10
        QTest::keyClick(&selector, Qt::Key_Down, Qt::ControlModifier); // height +1
        QTest::keyClick(&selector, Qt::Key_Return);
        QCOMPARE(selector.result(), int(QDialog::Accepted));
        QCOMPARE(selector.selectedRegion(), QRect(63, 60, 100, 41));

        RegionSelector edge(snapshot, screenRect, QRect(380, 10, 20, 20));
        edge.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edge));
        QTest::keyClick(&edge, Qt::Key_Right, Qt::ShiftModifier); // must stay on the screen
        QTest::keyClick(&edge, Qt::Key_Enter);
        QCOMPARE(edge.selectedRegion(), QRect(380, 10, 20, 20));

        RegionSelector empty(snapshot, screenRect);
        empty.show();
        QVERIFY(QTest::qWaitForWindowExposed(&empty));
        QTest::keyClick(&empty, Qt::Key_Return); // nothing to confirm
        QVERIFY(empty.isVisible());
        QTest::keyClick(&empty, Qt::Key_Escape);
        QCOMPARE(empty.result(), int(QDialog::Rejected));
        QVERIFY(empty.selectedRegion().isEmpty());
    }
    // F5: the captured rectangle is outlined briefly, click-through, and never captured.
    void captureRangeIsOutlinedBriefly() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:44px;");
        fixture.setAlignment(Qt::AlignCenter); fixture.resize(900, 160); fixture.move(30, 30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        QTest::qWait(250);
        MainWindow window;
        window.executable_.clear();
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_.clear(); window.consent_ = false;
        window.followGame_->setChecked(false);
        window.setRegion(fixture.screen(), QRect(fixture.mapToGlobal(QPoint()) - fixture.screen()->geometry().topLeft(), fixture.size()));
        QVERIFY(window.showCaptureRange_->isChecked());
        window.startCapture();
        QTRY_VERIFY_WITH_TIMEOUT(window.captureFlash_->isVisible(), 3000);
        const QRect source(fixture.mapToGlobal(QPoint()), fixture.size());
        QVERIFY(window.captureFlash_->geometry().contains(source));
        QVERIFY(!window.captureFlash_->geometry().adjusted(4, 4, -4, -4).contains(source)); // just outside, not a big box
        const LONG_PTR style = GetWindowLongPtrW(reinterpret_cast<HWND>(window.captureFlash_->winId()), GWL_EXSTYLE);
        QVERIFY(style & WS_EX_TRANSPARENT);
        QTRY_VERIFY_WITH_TIMEOUT(!window.captureFlash_->isVisible(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QVERIFY(window.original_->toPlainText().contains(QStringLiteral("天気"))); // the outline was not read
        window.showCaptureRange_->setChecked(false);
        window.startCapture();
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QVERIFY(!window.captureFlash_->isVisible());
    }

    // S3 (docs/plan/2026-11-01-ocr-speed.ko.md): our windows are hidden for a capture, with a 180 ms
    // wait, only when they would be in the picture. Elsewhere they stay and the capture starts at once.
    void captureHidesOnlyWindowsInThePicture() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        QLabel fixture(QStringLiteral("今日はいい天気です。"));
        fixture.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
        fixture.setStyleSheet("background:white;color:black;font-family:'Yu Gothic';font-size:44px;");
        fixture.setAlignment(Qt::AlignCenter); fixture.resize(900, 160); fixture.move(30, 30); fixture.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture));
        MainWindow window;
        window.executable_.clear();
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_.clear(); window.consent_ = false;
        window.followGame_->setChecked(false); window.showCaptureRange_->setChecked(false);
        window.inlineCaptions_->setChecked(false);
        window.setRegion(fixture.screen(), QRect(fixture.mapToGlobal(QPoint()) - fixture.screen()->geometry().topLeft(), fixture.size()));
        const QRect source(fixture.mapToGlobal(QPoint()), fixture.size());
        window.show(); window.move(source.bottomLeft() + QPoint(0, 120));
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.frameGeometry().intersects(source));
        window.startCapture();
        QVERIFY(window.isVisible()); // not in the picture: never hidden, so no flicker either
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QVERIFY(window.isVisible());
        QVERIFY2(window.original_->toPlainText().contains(QStringLiteral("天気")), qPrintable(window.original_->toPlainText()));
        window.move(source.topLeft()); // now over the subtitle area
        QTest::qWait(100);
        QVERIFY(window.frameGeometry().intersects(source));
        window.startCapture();
        QVERIFY(!window.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_, 25000);
        QVERIFY(window.isVisible()); // shown again after the capture
        QVERIFY2(window.original_->toPlainText().contains(QStringLiteral("天気")), qPrintable(window.original_->toPlainText()));
        window.hide();
    }
    // P-29 U1/U2: closing hides the window into the tray; the tray menu follows the buttons.
    void closeHidesToTrayAndMenuFollowsState() {
        MainWindow window;
        if (!window.tray_) QSKIP("No system tray on this desktop");
        window.executable_ = qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        QSettings().setValue("ui/trayNoticeShown", true); // no balloon during tests
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.close()); // ignored: the app keeps running
        QVERIFY(!window.isVisible());
        QVERIFY(window.tray_->isVisible());
        QVERIFY(window.ocrReady());
        window.trayHover_->trigger();
        QVERIFY(window.hoverMode_->isChecked());
        QVERIFY(window.trayHover_->isChecked());
        QCOMPARE(window.stateBadge_->text(), QStringLiteral("마우스 올려두기"));
        window.hoverMode_->setChecked(false); // the checkbox and the menu stay in step both ways
        QVERIFY(!window.trayHover_->isChecked());
        QCOMPARE(window.stateBadge_->text(), QStringLiteral("대기"));
    }
    // P-29 U5: shortcuts can be changed, are named in messages, and return to the defaults.
    void shortcutsCanBeChangedAndRestored() {
        unsigned modifiers = 0, key = 0;
        QVERIFY(!MainWindow::nativeKey(QKeySequence(QStringLiteral("A")), modifiers, key)); // would take game typing
        QVERIFY(!MainWindow::nativeKey(QKeySequence(QStringLiteral("Shift+A")), modifiers, key));
        QVERIFY(!MainWindow::nativeKey(QKeySequence(QStringLiteral("Ctrl+Space")), modifiers, key));
        QVERIFY(MainWindow::nativeKey(QKeySequence(QStringLiteral("F10")), modifiers, key));
        QCOMPARE(key, unsigned(VK_F10));
        QVERIFY(MainWindow::nativeKey(QKeySequence(QStringLiteral("Ctrl+Shift+5")), modifiers, key));
        QCOMPARE(key, unsigned('5'));
        QVERIFY(modifiers & MOD_SHIFT && modifiers & MOD_CONTROL && !(modifiers & MOD_ALT));
        MainWindow window;
        const QKeySequence chosen(QStringLiteral("Ctrl+Alt+Shift+F11"));
        QSettings().setValue("hotkeys/auto", chosen.toString(QKeySequence::PortableText));
        window.registerHotkeys();
        QVERIFY(window.autoRegistered_);
        QCOMPARE(window.autoShortcut_, chosen.toString(QKeySequence::NativeText));
        QVERIFY2(window.auto_->text().contains(window.autoShortcut_), qPrintable(window.auto_->text()));
        window.setStatus(QStringLiteral("자동 번역 중 · Ctrl + Alt + P로 멈춥니다."));
        QVERIFY2(window.status_->text().contains(window.autoShortcut_), qPrintable(window.status_->text()));
        QSettings().remove("hotkeys/auto");
        window.registerHotkeys();
        QCOMPARE(window.autoShortcut_, QStringLiteral("Ctrl+Alt+P"));
    }
    // P-29 U6: a bigger caption and a translucent panel keep the placement rules.
    void captionStyleScalesTextAndKeepsPlacement() {
        SubtitleOverlay overlay;
        const QRect source(100, 100, 600, 40), work(0, 0, 1920, 1080);
        QVERIFY(overlay.showText(QStringLiteral("자막 크기 확인"), source, work));
        const int normal = overlay.height();
        overlay.setStyle(1.5, true);
        QVERIFY(overlay.showText(QStringLiteral("자막 크기 확인"), source, work));
        QVERIFY(overlay.height() > normal);
        QVERIFY(overlay.geometry().top() > source.bottom()); // still under the original line
        QVERIFY(!overlay.geometry().intersects(source));
        overlay.hide();
    }
    // P-29 U3/U4: the settings window has its pages, and each fits. SUBTITLE_TEST_SCREENSHOTS saves them.
    void settingsPagesAreComplete() {
        MainWindow window;
        QCOMPARE(window.settingsPages_->count(), 8);
        const QString shots = qEnvironmentVariable("SUBTITLE_TEST_SCREENSHOTS");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (!shots.isEmpty()) {
            window.grab().save(QDir(shots).filePath("main.png"));
            // The stage with a line, and the auto switch drawn on (state only, no capture runs).
            window.stage_->setContent(QStringLiteral("今日はいい天気ですね。"), QStringLiteral("오늘은 날씨가 좋네요."));
            {
                const QSignalBlocker blocker(window.auto_);
                window.auto_->setChecked(true);
            }
            window.grab().save(QDir(shots).filePath("main-line.png"));
            const QSignalBlocker blocker(window.auto_);
            window.auto_->setChecked(false);
        }
        window.reloadSettings_();
        window.settingsWindow_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.settingsWindow_));
        for (int i = 0; i < window.settingsPages_->count(); ++i) {
            window.settingsPages_->setCurrentRow(i);
            QTest::qWait(30);
            if (!shots.isEmpty()) window.settingsWindow_->grab().save(QDir(shots).filePath(QStringLiteral("settings-%1.png").arg(i)));
        }
        window.settingsWindow_->hide();
        window.hide();
    }
    void fallbackResultWindowDoesNotBlockAutoCapture() {
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        if (!window.ocrReady()) QSKIP("Actual engine not configured");
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=QGuiApplication::primaryScreen();
        window.selectedGeometry_=window.selectedScreen_->geometry();
        window.selectedDpr_=window.selectedScreen_->devicePixelRatio();
        window.region_=QRect(80,100,600,50);
        window.inlineCaptions_->setChecked(false);
        window.setAutoTranslate(true);
        window.activeSource_=ST_SCREEN_CAPTURE;
        window.captionArea_=window.selectedArea();
        window.result_->setPlainText(QStringLiteral("위치 검증용 표시 · API 번역 아님"));
        window.resultWindow_->move(window.selectedGeometry_.topLeft()+QPoint(80,100));
        window.presentTranslation();
        QTest::qWait(100);
        const QRect source=window.region_.translated(window.selectedGeometry_.topLeft());
        QVERIFY(window.resultWindow_->isVisible());
        QVERIFY(!window.resultWindow_->frameGeometry().intersects(source));
        QVERIFY(window.autoTranslating());
        // Also recover when a user later drags the window onto the source.
        window.resultWindow_->move(source.topLeft());
        window.autoTick();
        QVERIFY(!window.resultWindow_->frameGeometry().intersects(source));
        QVERIFY(window.autoTranslating());
        window.setAutoTranslate(false);
    }
    void noRoomForResultStopsAutoWithExplanation() {
        if (QGuiApplication::screens().size()!=1) QSKIP("No-room test requires one physical monitor");
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        if (!window.ocrReady()) QSKIP("Actual engine not configured");
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=QGuiApplication::primaryScreen();
        window.selectedGeometry_=window.selectedScreen_->geometry();
        window.selectedDpr_=window.selectedScreen_->devicePixelRatio();
        window.region_=QRect(QPoint(),window.selectedGeometry_.size());
        window.setAutoTranslate(true);
        window.activeSource_=ST_SCREEN_CAPTURE;
        window.captionArea_=window.selectedArea();
        window.result_->setPlainText(QStringLiteral("배치 검증용 표시 · API 번역 아님"));
        window.presentTranslation(); // Caption placement fails for a full-monitor source.
        QVERIFY(window.resultWindow_->isVisible());
        QVERIFY(!window.autoTranslating());
        QVERIFY(window.resultStatus_->text().contains(QStringLiteral("자동 번역을 멈")));
        QVERIFY(window.status_->text().contains(QStringLiteral("영역")));
        // A later completion status must not erase the actionable reason.
        window.finish(QStringLiteral("번역 완료"));
        QVERIFY(window.status_->text().contains(QStringLiteral("영역")));
    }
    void blankAutoFrameNeverStartsTranslation() {
        MainWindow window;
        window.executable_=qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        window.tessdata_=qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        window.paddleDir_=qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.applyOcrEngine();
        window.apiKey_.clear(); window.consent_=false;
        window.selectedScreen_=QGuiApplication::primaryScreen();
        window.selectedGeometry_=window.selectedScreen_->geometry();
        window.selectedDpr_=window.selectedScreen_->devicePixelRatio();
        window.region_=QRect(80,100,600,50);
        window.setAutoTranslate(true);
        // Keep auto mode active but do not poll unrelated desktop pixels during this image-path test.
        window.autoTimer_->setInterval(60000);
        QImage image(200,60,QImage::Format_RGB32); image.fill(Qt::white);
        window.processImage(image,ST_SCREEN_CAPTURE);
        QTRY_VERIFY_WITH_TIMEOUT(!window.busy_,5000);
        QVERIFY(window.original_->toPlainText().isEmpty());
        QVERIFY(window.result_->toPlainText().isEmpty());
        QVERIFY(!window.translation_.isBusy());
        QVERIFY(!window.subtitleOverlay_->isVisible());
        QVERIFY(window.autoTranslating());
        QVERIFY(window.status_->text().contains(QStringLiteral("글자")));
        window.setAutoTranslate(false);
    }
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
        auto state = [&] {
            return QStringLiteral("read [%1] engine %2 confidence %3 busy %4 status [%5]")
                .arg(window.original_->toPlainText(), window.ocr_.engineUsed()).arg(window.ocr_.tesseractConfidence())
                .arg(window.busy_).arg(window.status_->text());
        };
        QTRY_VERIFY2_WITH_TIMEOUT(window.original_->toPlainText().contains(QStringLiteral("天気")) && !window.busy_,
                                  qPrintable(state()), 25000);
        const auto firstRequest=window.request_;
        QTest::qWait(1600); // The unchanged line must not be read again.
        QCOMPARE(window.request_,firstRequest);
        fixture.setText(QStringLiteral("明日は雨が降ります。"));
        QTRY_VERIFY2_WITH_TIMEOUT(window.request_!=firstRequest && !window.busy_ && !window.original_->toPlainText().isEmpty(),
                                  qPrintable(state()), 25000);
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
