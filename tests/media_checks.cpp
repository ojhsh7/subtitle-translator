// Stage 0 shared parts: translation queue, usage, history, hotkeys, diagnostics, single instance,
// video change detection and capture exclusion. Network tests use the loopback fixture only.
#include "local_http_fixture.h"
#include "capture_service.h"
#include "capture_worker.h"
#include "change_detector.h"
#include "diagnostics.h"
#include "history_store.h"
#include "hotkey_manager.h"
#include "single_instance.h"
#include "translation_queue.h"
#include "translation_service.h"
#include "usage_meter.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <QtTest>
#include <windows.h>

namespace {
QImage subtitleFrame(const QString& text, int backgroundShift, const QSize& size = QSize(820, 140)) {
    QImage image(size, QImage::Format_RGB32);
    QPainter p(&image);
    // Moving soft picture: gradients shift every frame like video content.
    QLinearGradient gradient(QPointF(backgroundShift % 400, 0), QPointF(backgroundShift % 400 + 600, size.height()));
    gradient.setColorAt(0, QColor(40, 90 + backgroundShift % 60, 120));
    gradient.setColorAt(1, QColor(150, 110, 60 + backgroundShift % 80));
    p.fillRect(image.rect(), gradient);
    if (!text.isEmpty()) {
        QFont font("Segoe UI"); font.setPixelSize(40); font.setBold(true); p.setFont(font);
        QPainterPath path;
        path.addText(40, 90, font, text);
        p.setRenderHint(QPainter::Antialiasing);
        p.strokePath(path, QPen(Qt::black, 5));
        p.fillPath(path, Qt::white);
    }
    return image;
}
}

class ServiceChecks : public QObject {
    Q_OBJECT
    LocalHttpFixture* server_ = nullptr;
    LoopbackNetwork* network_ = nullptr;
    UsageMeter* usage_ = nullptr;
    QString key_ = QStringLiteral("fixture-key:fx");
    TranslationQueue* queue(int parallel = 2) {
        auto* q = new TranslationQueue([this](QObject* parent) {
            auto* t = new TranslationService(parent, network_);
            t->setCredentials(key_, true);
            return static_cast<ITranslator*>(t);
        }, usage_, this);
        q->setMaxParallel(parallel);
        q->setRetryDelays({50, 100, 200});
        return q;
    }
    static QStringList numbered(int count, const QString& prefix = QStringLiteral("line")) {
        QStringList texts;
        for (int i = 0; i < count; ++i) texts << QStringLiteral("%1 %2").arg(prefix).arg(i);
        return texts;
    }
private slots:
    void initTestCase() {
        qApp->setOrganizationName("SubtitleTranslatorChecks");
        qApp->setApplicationName("TemporaryMediaChecks");
        QSettings().clear();
    }
    void init() {
        QSettings().clear();
        server_ = new LocalHttpFixture;
        QVERIFY(server_->listen(QHostAddress::LocalHost, 0));
        server_->handler = [](const QByteArray&, const QByteArray& body) { return LocalHttpFixture::echo(body); };
        network_ = new LoopbackNetwork(server_->serverPort());
        usage_ = new UsageMeter(this);
        key_ = QStringLiteral("fixture-key:fx");
    }
    void cleanup() { delete usage_; delete network_; delete server_; }
    void cleanupTestCase() { QSettings().clear(); }

    // ---------- usage ----------
    void usageCountsCharactersAndEnforcesCap() {
        QCOMPARE(UsageMeter::characters(QStringLiteral("안녕 😀")), qint64(4)); // emoji is one character
        QVERIFY(usage_->allows(1000000));
        usage_->record(30);
        QCOMPARE(usage_->session(), qint64(30));
        QCOMPARE(usage_->month(), qint64(30));
        usage_->setCap(true, 40);
        QVERIFY(usage_->allows(10));
        QVERIFY(!usage_->allows(11));
        QVERIFY(usage_->capReachedMessage().contains(QStringLiteral("멈췄습니다")));
        UsageMeter next; // the month total survives, the session does not
        QCOMPARE(next.session(), qint64(0));
        QCOMPARE(next.month(), qint64(30));
    }

    // ---------- translation queue ----------
    void planRespectsCountAndSize() {
        const auto ranges = TranslationQueue::plan(numbered(120), 50, 30000);
        QCOMPARE(ranges.size(), 3);
        QCOMPARE(ranges[0], qMakePair(0, 50));
        QCOMPARE(ranges[2], qMakePair(100, 120));
        const QStringList big{QString(20000, 'a'), QString(20000, 'b'), QString(5, 'c')};
        const auto sized = TranslationQueue::plan(big, 50, 30000);
        QCOMPARE(sized.size(), 2);
        QCOMPARE(sized[0], qMakePair(0, 1));
        QCOMPARE(sized[1], qMakePair(1, 3));
    }
    void batchKeepsOrderAndCachesRepeats() {
        auto* q = queue();
        QSignalSpy finished(q, &TranslationQueue::finished);
        QSignalSpy progress(q, &TranslationQueue::progress);
        QStringList texts = numbered(120);
        texts << QStringLiteral("line 3") << QString() << QStringLiteral("  "); // repeat + blanks
        const quint64 id = q->translateBatch(texts, {});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(finished.first().at(0).toULongLong(), id);
        const QStringList out = finished.first().at(1).toStringList();
        QCOMPARE(out.size(), texts.size());
        QCOMPARE(out[0], QStringLiteral("[KO] line 0"));
        QCOMPARE(out[119], QStringLiteral("[KO] line 119"));
        QCOMPARE(out[120], QStringLiteral("[KO] line 3"));
        QCOMPARE(out[121], QString());
        QCOMPARE(out[122], QStringLiteral("  "));
        QCOMPARE(server_->requests.size(), 3); // 120 distinct texts, 50 per request
        QCOMPARE(usage_->session(), UsageMeter::characters(numbered(120)));
        QVERIFY(progress.size() >= 3);
        // Same text again: answered from the cache, quickly and without a request.
        QElapsedTimer timer; timer.start();
        q->translateLive({QStringLiteral("line 7")}, {}, QStringLiteral("screen"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 1000);
        QVERIFY2(timer.elapsed() < 100, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(finished.last().at(1).toStringList(), QStringList{QStringLiteral("[KO] line 7")});
        QVERIFY(finished.last().at(3).toBool());
        QCOMPARE(server_->requests.size(), 3);
    }
    void requestCarriesContextAndTagHandling() {
        auto* q = queue();
        QSignalSpy finished(q, &TranslationQueue::finished);
        TranslateOptions options;
        options.target = QStringLiteral("KO"); options.source = QStringLiteral("EN");
        options.context = QStringLiteral("Previous line.");
        options.xmlTags = true;
        q->translateLive({QStringLiteral("Hello <x>@alice</x>")}, options, QStringLiteral("screen"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        const QJsonObject body = QJsonDocument::fromJson(LocalHttpFixture::bodyOf(server_->requests.first())).object();
        QCOMPARE(body.value("context").toString(), QStringLiteral("Previous line."));
        QCOMPARE(body.value("tag_handling").toString(), QStringLiteral("xml"));
        QCOMPARE(body.value("ignore_tags").toArray().first().toString(), QStringLiteral("x"));
        QCOMPARE(body.value("source_lang").toString(), QStringLiteral("EN"));
        QCOMPARE(finished.first().at(1).toStringList().first(), QStringLiteral("[KO] Hello <x>@alice</x>"));
    }
    void batchRetriesRateLimitButLiveDoesNot() {
        auto* q = queue(1);
        QSignalSpy finished(q, &TranslationQueue::finished);
        QSignalSpy failed(q, &TranslationQueue::failed);
        server_->replies.enqueue({429, "{}", 0});
        q->translateBatch(numbered(3), {});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(server_->requests.size(), 2); // one 429, one retry
        QCOMPARE(failed.size(), 0);
        server_->replies.enqueue({429, "{}", 0});
        q->translateLive({QStringLiteral("live")}, {}, QStringLiteral("screen"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY(failed.first().at(2).toBool()); // automatic modes must stop
        QTest::qWait(300);
        QCOMPARE(server_->requests.size(), 3);
    }
    void quotaStopsBatchAtOnce() {
        auto* q = queue(1);
        QSignalSpy failed(q, &TranslationQueue::failed);
        server_->replies.enqueue({456, "{}", 0});
        q->translateBatch(numbered(80), {});
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("한도")));
        QVERIFY(failed.first().at(2).toBool());
        QTest::qWait(400);
        QCOMPARE(server_->requests.size(), 1); // the second part was never sent
    }
    void sessionCapIsCheckedBeforeSending() {
        usage_->setCap(true, 10);
        auto* q = queue();
        QSignalSpy failed(q, &TranslationQueue::failed);
        q->translateBatch({QStringLiteral("twenty characters!!!")}, {});
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("상한")));
        QCOMPARE(server_->requests.size(), 0);
    }
    void missingKeyNeverSends() {
        key_.clear();
        auto* q = queue();
        QSignalSpy failed(q, &TranslationQueue::failed);
        q->translateLive({QStringLiteral("text")}, {}, QStringLiteral("screen"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("키")));
        QCOMPARE(server_->requests.size(), 0);
    }
    void newerLiveLineReplacesWaitingOne() {
        server_->handler = [](const QByteArray&, const QByteArray& body) { return LocalHttpFixture::echo(body, "[KO] ", 300); };
        auto* q = queue(1);
        QSignalSpy finished(q, &TranslationQueue::finished);
        QSignalSpy dropped(q, &TranslationQueue::dropped);
        const quint64 a = q->translateLive({QStringLiteral("A")}, {}, QStringLiteral("screen"));
        const quint64 b = q->translateLive({QStringLiteral("B")}, {}, QStringLiteral("screen"));
        const quint64 c = q->translateLive({QStringLiteral("C")}, {}, QStringLiteral("screen"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(dropped.first().first().toULongLong(), b);
        QCOMPARE(finished[0].first().toULongLong(), a);
        QCOMPARE(finished[1].first().toULongLong(), c);
        QCOMPARE(server_->requests.size(), 2);
    }
    void cancelledJobNeverReports() {
        server_->handler = [](const QByteArray&, const QByteArray& body) { return LocalHttpFixture::echo(body, "[KO] ", 300); };
        auto* q = queue();
        QSignalSpy finished(q, &TranslationQueue::finished);
        QSignalSpy failed(q, &TranslationQueue::failed);
        const quint64 id = q->translateBatch(numbered(5), {});
        QTest::qWait(50);
        q->cancel(id);
        QTest::qWait(600);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(failed.size(), 0);
        QVERIFY(!q->busy());
    }

    // ---------- history ----------
    void historyExportsSrtAndText() {
        HistoryStore history;
        const quint64 a = history.add("video", "Hello.", "안녕.", 10000);
        history.setEnd(a, 11500);
        history.add("video", "Bye.", "잘 가.", 12000);
        const QString srt = HistoryStore::toSrt(history.entries());
        QVERIFY2(srt.startsWith("1\n00:00:00,000 --> 00:00:01,500\n안녕.\n\n2\n00:00:02,000 --> 00:00:06,000\n잘 가.\n"), qPrintable(srt));
        QVERIFY(HistoryStore::toText(history.entries()).contains("Hello.\n안녕."));
        QCOMPARE(HistoryStore::srtTime(3723004), QStringLiteral("01:02:03,004"));
    }
    void historyIsDeletedUnlessKept() {
        QTemporaryDir dir;
        const QString path = dir.filePath("history.jsonl");
        {
            HistoryStore history;
            history.setPersistent(true, path);
            history.add("text", "a", "가");
            history.add("text", "b", "나");
            QVERIFY(QFile::exists(path));
        }
        {
            HistoryStore again;
            again.setPersistent(true, path);
            QCOMPARE(again.entries().size(), 2);
            QCOMPARE(again.entries().last().translation, QStringLiteral("나"));
            again.setPersistent(false, path);
            QVERIFY(!QFile::exists(path));
        }
    }

    // ---------- hotkeys ----------
    void hotkeysNeedCtrlOrAltAndNoDuplicates() {
        unsigned m = 0, vk = 0;
        QVERIFY(HotkeyManager::toNative(QKeySequence("Ctrl+Alt+T"), &m, &vk));
        QCOMPARE(vk, unsigned('T'));
        QVERIFY(m & MOD_CONTROL); QVERIFY(m & MOD_ALT); QVERIFY(m & MOD_NOREPEAT);
        QVERIFY(HotkeyManager::toNative(QKeySequence("Ctrl+Alt+F9"), &m, &vk));
        QCOMPARE(vk, unsigned(VK_F9));
        QVERIFY(!HotkeyManager::toNative(QKeySequence("T"), &m, &vk));
        QVERIFY(!HotkeyManager::toNative(QKeySequence("Shift+T"), &m, &vk));
        QVERIFY(!HotkeyManager::toNative(QKeySequence("Ctrl+Alt+Space"), &m, &vk));
        HotkeyManager hotkeys;
        QCOMPARE(hotkeys.sequence(HotkeyManager::ModeVideo), QKeySequence("Ctrl+Alt+2"));
        QString problem;
        QVERIFY(!hotkeys.setSequence(HotkeyManager::ModeVideo, QKeySequence("Ctrl+Alt+T"), &problem));
        QVERIFY(problem.contains(QStringLiteral("이미")));
        QVERIFY(hotkeys.setSequence(HotkeyManager::ModeVideo, QKeySequence("Ctrl+Alt+V"), &problem));
        QCOMPARE(HotkeyManager().sequence(HotkeyManager::ModeVideo), QKeySequence("Ctrl+Alt+V"));
        QVERIFY(hotkeys.setSequence(HotkeyManager::ModeText, QKeySequence(), &problem)); // removed on purpose
        QVERIFY(hotkeys.sequence(HotkeyManager::ModeText).isEmpty());
        hotkeys.resetToDefaults();
        QCOMPARE(hotkeys.sequence(HotkeyManager::ModeText), QKeySequence("Ctrl+Alt+3"));
    }
    void takenHotkeyFallsBackOrIsReported() {
        // Hold Ctrl+Alt+M like another app would; the default falls back to F8, a user key is reported.
        // On some PCs another program (seen: a game overlay) already holds Ctrl+Alt+M; that is the same case.
        QWidget other, mine;
        const HWND otherHandle = reinterpret_cast<HWND>(other.winId());
        const bool heldHere = RegisterHotKey(otherHandle, 77, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M') != 0;
        QVERIFY(!HotkeyManager::availableSystemWide(QKeySequence("Ctrl+Alt+M")));
        HotkeyManager hotkeys;
        hotkeys.registerAll(quintptr(mine.winId()));
        if (HotkeyManager::availableSystemWide(QKeySequence("Ctrl+Alt+F8")) || hotkeys.isRegistered(HotkeyManager::Mouse))
            QCOMPARE(hotkeys.active(HotkeyManager::Mouse), QKeySequence("Ctrl+Alt+F8"));
        // A key the user chose is never silently replaced: it is reported as taken.
        QVERIFY(RegisterHotKey(otherHandle, 78, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'K'));
        QString problem;
        QVERIFY(hotkeys.setSequence(HotkeyManager::Hover, QKeySequence("Ctrl+Alt+K"), &problem));
        hotkeys.registerAll(quintptr(mine.winId()));
        QVERIFY(!hotkeys.isRegistered(HotkeyManager::Hover));
        QVERIFY(hotkeys.failed().contains(HotkeyManager::Hover));
        QCOMPARE(hotkeys.text(HotkeyManager::Hover), QStringLiteral("단축키 없음"));
        hotkeys.unregisterAll();
        hotkeys.resetToDefaults();
        UnregisterHotKey(otherHandle, 78);
        if (heldHere) {
            UnregisterHotKey(otherHandle, 77);
            QVERIFY(HotkeyManager::availableSystemWide(QKeySequence("Ctrl+Alt+M")));
        }
    }

    // ---------- diagnostics ----------
    void logRotatesAndHidesText() {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        for (int i = 0; i < 4; ++i) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray::number(i)); file.close();
            Diagnostics::rotate(path, 3);
        }
        QVERIFY(!QFile::exists(path));
        QVERIFY(QFile::exists(dir.filePath("app.1.log")));
        QVERIFY(QFile::exists(dir.filePath("app.2.log")));
        QVERIFY(!QFile::exists(dir.filePath("app.3.log"))); // never more than three files
        QFile newest(dir.filePath("app.1.log")); QVERIFY(newest.open(QIODevice::ReadOnly));
        QCOMPARE(newest.readAll(), QByteArray("3"));
        QCOMPARE(Diagnostics::sensitive(QStringLiteral("비밀 문장")), QStringLiteral("<5 chars>"));
    }
    void stallMonitorSeesABlockedThread() {
        UiStallMonitor monitor;
        monitor.start();
        QTest::qWait(100);
        QCOMPARE(monitor.stalls(), 0);
        QThread::msleep(150); // the GUI thread does nothing for 150 ms
        QTest::qWait(60);
        QCOMPARE(monitor.stalls(), 1);
        QVERIFY(monitor.worstMs() >= 100);
    }

    // ---------- single instance ----------
    void secondLaunchHandsOverItsArguments() {
        const QString key = QStringLiteral("checks-%1").arg(QCoreApplication::applicationPid());
        SingleInstance primary(key);
        QVERIFY(primary.claim({}));
        QSignalSpy activated(&primary, &SingleInstance::activated);
        auto second = QtConcurrent::run([key] {
            SingleInstance other(key);
            return other.claim({QStringLiteral("--auto")}, 3000);
        });
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 1, 5000);
        QCOMPARE(activated.first().first().toStringList(), QStringList{QStringLiteral("--auto")});
        second.waitForFinished();
        QVERIFY(!second.result()); // the second launch must exit
    }

    // ---------- video change detection ----------
    void movingPictureBehindAStaticLineIsReadOnce() {
        ChangeDetector detector(ChangeDetector::videoProfile());
        int processed = 0;
        for (int tick = 0; tick < 40; ++tick) // 10 s at 0.25 s
            processed += detector.feed(subtitleFrame("Where are you going?", tick * 37), tick * 250) == ChangeDetector::Decision::Process;
        QCOMPARE(processed, 1);
    }
    void replacedLineIsReadAfterOneQuietPoll() {
        ChangeDetector detector(ChangeDetector::videoProfile());
        QCOMPARE(detector.feed(subtitleFrame("First line here.", 0), 0), ChangeDetector::Decision::Wait);
        QCOMPARE(detector.feed(subtitleFrame("First line here.", 10), 250), ChangeDetector::Decision::Process);
        QCOMPARE(detector.feed(subtitleFrame("Completely new words!", 20), 500), ChangeDetector::Decision::Wait);
        QCOMPARE(detector.feed(subtitleFrame("Completely new words!", 30), 750), ChangeDetector::Decision::Process);
    }
    void wordsBeingAddedWaitForTheFullLine() {
        ChangeDetector detector(ChangeDetector::videoProfile());
        const QStringList words{"So", "So we", "So we went", "So we went home"};
        qint64 now = 0;
        detector.feed(subtitleFrame(words[0], 0), now); now += 250;
        QCOMPARE(detector.feed(subtitleFrame(words[0], 0), now), ChangeDetector::Decision::Process); // first word after a blank
        int reads = 0;
        for (int w = 1; w < words.size(); ++w)
            for (int t = 0; t < 2; ++t) { now += 250; reads += detector.feed(subtitleFrame(words[w], 0), now) == ChangeDetector::Decision::Process; }
        QCOMPARE(reads, 0); // still growing (< 0.7 s quiet)
        for (int t = 0; t < 4; ++t) { now += 250; reads += detector.feed(subtitleFrame(words.last(), 0), now) == ChangeDetector::Decision::Process; }
        QCOMPARE(reads, 1);
    }
    void clearedLineIsReportedAsBlankOnce() {
        ChangeDetector detector(ChangeDetector::videoProfile());
        detector.feed(subtitleFrame("Some words", 0), 0);
        QCOMPARE(detector.feed(subtitleFrame("Some words", 0), 250), ChangeDetector::Decision::Process);
        QCOMPARE(detector.feed(subtitleFrame(QString(), 5), 500), ChangeDetector::Decision::Wait);
        QCOMPARE(detector.feed(subtitleFrame(QString(), 9), 750), ChangeDetector::Decision::Blank);
        QCOMPARE(detector.feed(subtitleFrame(QString(), 13), 1000), ChangeDetector::Decision::Unchanged);
    }

    // ---------- capture ----------
    void workerCaptureMatchesQtGrabAndSkipsExcludedWindows() {
        QLabel back(QStringLiteral("BACK")); back.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        back.setStyleSheet("background:#0000ff;"); back.resize(400, 200); back.move(60, 60); back.show();
        QLabel front; front.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        front.setStyleSheet("background:#ff0000;"); front.resize(200, 100); front.move(110, 110); front.show();
        QVERIFY(QTest::qWaitForWindowExposed(&front));
        QTest::qWait(250);
        QScreen* screen = back.screen();
        const QRect local = QRect(QPoint(130, 130) - screen->geometry().topLeft(), QSize(40, 40));
        QString error;
        const QImage before = Capture::physical(Capture::physicalRect(screen, local), screen->devicePixelRatio(), error);
        QVERIFY2(!before.isNull(), qPrintable(error));
        QCOMPARE(QColor(before.pixel(5, 5)), QColor(Qt::red));
        const QImage qt = Capture::region(screen, local, error);
        QCOMPARE(before.size(), qt.size());
        QCOMPARE(before.convertToFormat(QImage::Format_RGB32), qt.convertToFormat(QImage::Format_RGB32));
        if (!Capture::exclusionSupported()) QSKIP("WDA_EXCLUDEFROMCAPTURE needs Windows 10 2004 or later");
        QVERIFY(Capture::setExcludedFromCapture(&front, true));
        QVERIFY(Capture::isExcludedFromCapture(&front));
        QTest::qWait(250);
        const QImage after = Capture::physical(Capture::physicalRect(screen, local), screen->devicePixelRatio(), error);
        QCOMPARE(QColor(after.pixel(5, 5)), QColor(Qt::blue)); // our window is visible but not captured
        QCOMPARE(QColor(Capture::region(screen, local, error).pixel(5, 5)), QColor(Qt::blue)); // Qt path too
    }
    void workerReportsSettledFramesOffTheGuiThread() {
        QLabel line(QStringLiteral("今日はいい天気です。"));
        line.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        line.setStyleSheet("background:white;color:black;font-size:40px;");
        line.resize(600, 80); line.move(40, 300); line.show();
        QVERIFY(QTest::qWaitForWindowExposed(&line));
        QTest::qWait(200);
        CaptureWorker worker;
        QSignalSpy frames(&worker, &CaptureWorker::frameReady);
        QScreen* screen = line.screen();
        const QRect physical = Capture::physicalRect(screen, QRect(line.mapToGlobal(QPoint()) - screen->geometry().topLeft(), line.size()));
        for (int tick = 0; tick < 4; ++tick) {
            QTRY_VERIFY(!worker.checking());
            QVERIFY(worker.requestCheck(physical, screen->devicePixelRatio(), tick * 400, 7));
            QTRY_VERIFY(!worker.checking());
        }
        QTRY_COMPARE(frames.size(), 1); // two quiet polls, then exactly one read
        QCOMPARE(frames.first().at(2).toULongLong(), quint64(7));
        QCOMPARE(frames.first().first().value<QImage>().size(), physical.size());
    }
};
QTEST_MAIN(ServiceChecks)
#include "media_checks.moc"
