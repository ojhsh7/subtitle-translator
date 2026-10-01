// Stage 1 video mode on a fake video player: real screen capture, real PaddleOCR, local translation
// fixture. The player paints a moving picture and 1-4 s subtitles (outlined, on a translucent band,
// and word by word) and records when each line really appeared, so misses, false text in the gaps
// and latencies can be measured. SUBTITLE_VIDEO_LINES=200 runs the acceptance length and
// SUBTITLE_VIDEO_REPORT=<file.json> writes the measurements.
#include "local_http_fixture.h"
#include "diagnostics.h"
#include "game_window.h"
#include "main_window.h"
#include "subtitle_presenter.h"
#include "video_mode.h"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <cmath>

namespace {
struct Line { QString text; qint64 start = 0, duration = 0; int style = 0; qint64 shownAt = -1, clearedAt = -1; };
QString normalize(const QString& text) {
    QString out;
    for (QChar c : text.toLower()) if (c.isLetterOrNumber()) out += c;
    return out;
}
double similarity(const QString& a, const QString& b) {
    const QString x = normalize(a), y = normalize(b);
    if (x.isEmpty() || y.isEmpty()) return 0;
    std::vector<int> prev(y.size() + 1), cur(y.size() + 1);
    for (int j = 0; j <= y.size(); ++j) prev[j] = j;
    for (int i = 1; i <= x.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= y.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return 1.0 - double(prev[y.size()]) / std::max(x.size(), y.size());
}
// Deterministic sentences, all different, 4-9 words.
QStringList sentences(int count) {
    const QStringList subjects{"The captain", "My sister", "That old robot", "Our neighbour", "The new teacher", "A tired pilot",
                               "The small dragon", "Everyone here", "The reporter", "His best friend", "The village chief", "This machine"};
    const QStringList verbs{"opened", "found", "painted", "carried", "forgot", "repaired", "watched", "followed", "borrowed", "cleaned"};
    const QStringList objects{"the blue door", "a strange letter", "the heavy box", "every window", "the broken radio",
                              "a golden key", "the last train", "our secret map", "the quiet garden", "a wooden boat", "the red lamp"};
    const QStringList endings{"before dawn.", "again today.", "near the river.", "without a word.", "last night.", "in the rain.",
                              "for the king.", "very slowly.", "after lunch.", "on Monday."};
    QStringList out;
    quint32 seed = 12345;
    auto next = [&seed](int n) { seed = seed * 1103515245u + 12345u; return int((seed >> 16) % quint32(n)); };
    while (out.size() < count) {
        const QString s = subjects[next(subjects.size())] + ' ' + verbs[next(verbs.size())] + ' ' +
                          objects[next(objects.size())] + ' ' + endings[next(endings.size())];
        if (!out.contains(s)) out << s;
    }
    return out;
}
class FakePlayer final : public QWidget {
public:
    QList<Line> lines;
    QElapsedTimer clock;
    qint64 origin = 0; // Diagnostics::nowMs() at clock start
    int frame = 0;
    QString shownText;
    FakePlayer() : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool) {
        setAttribute(Qt::WA_ShowWithoutActivating);
        connect(&timer_, &QTimer::timeout, this, [this] { ++frame; update(); });
    }
    void start() { clock.start(); origin = Diagnostics::nowMs(); timer_.start(33); }
    QRect band() const { return QRect(40, height() * 76 / 100, width() - 80, height() * 20 / 100); }
    bool finished() const { return !lines.isEmpty() && clock.elapsed() > lines.last().start + lines.last().duration + 1500; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        // Moving picture: a gradient sweep and drifting shapes, like video content behind captions.
        QLinearGradient g(QPointF((frame * 7) % width(), 0), QPointF((frame * 7) % width() + width(), height()));
        g.setColorAt(0, QColor(30 + frame % 40, 70, 110)); g.setColorAt(0.5, QColor(170, 120 + frame % 60, 70)); g.setColorAt(1, QColor(60, 140, 90));
        p.fillRect(rect(), g);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < 6; ++i) {
            p.setBrush(QColor(80 + i * 25, 60 + (frame + i * 30) % 120, 160 - i * 15));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF((frame * (3 + i) + i * 150) % (width() + 200) - 100, 60 + i * 55 + 20 * std::sin(frame / 15.0 + i)), 70, 45);
        }
        const qint64 now = clock.elapsed();
        QString text, full;
        int style = 0;
        for (Line& line : lines) {
            if (now < line.start || now >= line.start + line.duration) {
                if (now >= line.start + line.duration && line.shownAt >= 0 && line.clearedAt < 0) line.clearedAt = origin + now;
                continue;
            }
            style = line.style;
            text = full = line.text;
            if (style == 2) { // auto-generated captions: one more word every 300 ms
                const QStringList words = line.text.split(' ');
                text = words.mid(0, std::min<int>(int(words.size()), int(1 + (now - line.start) / 300))).join(' ');
            }
            if (line.shownAt < 0) line.shownAt = origin + now;
            break;
        }
        shownText = text;
        if (text.isEmpty()) return;
        QFont font("Segoe UI"); font.setPixelSize(34); font.setBold(true);
        const QRect area = band();
        if (style == 1) { // translucent caption box, like YouTube's default style
            const int w = QFontMetrics(font).horizontalAdvance(text) + 40;
            p.fillRect(QRect(area.center().x() - w / 2, area.center().y() - 26, w, 52), QColor(8, 8, 8, 190));
        }
        QPainterPath path;
        // Word-by-word captions keep earlier words in place: centre on the finished line.
        const int x = area.center().x() - QFontMetrics(font).horizontalAdvance(full) / 2;
        path.addText(x, area.center().y() + 12, font, text);
        if (style != 1) p.strokePath(path, QPen(Qt::black, 5));
        p.fillPath(path, Qt::white);
    }
private:
    QTimer timer_;
};
double percentile(QList<qint64> values, double p) {
    if (values.isEmpty()) return -1;
    std::sort(values.begin(), values.end());
    return double(values[std::min<qsizetype>(values.size() - 1, qsizetype(p * (values.size() - 1) + 0.5))]);
}
}

class VideoChecks : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qApp->setProperty("testMode", true);
        qApp->setOrganizationName("SubtitleTranslatorChecks");
        qApp->setApplicationName("TemporaryVideoChecks");
        qApp->setQuitOnLastWindowClosed(false);
        QSettings().clear();
    }
    void cleanupTestCase() { QSettings().clear(); }
    void fakePlayerSubtitlesAreReadTranslatedAndShown() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("PaddleOCR models not configured");
        const int count = qEnvironmentVariableIntValue("SUBTITLE_VIDEO_LINES") > 0 ? qEnvironmentVariableIntValue("SUBTITLE_VIDEO_LINES") : 12;
        const int translateDelay = qEnvironmentVariableIntValue("SUBTITLE_VIDEO_TRANSLATE_MS");
        LocalHttpFixture server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        server.handler = [translateDelay](const QByteArray&, const QByteArray& body) { return LocalHttpFixture::echo(body, "[KO] ", translateDelay); };
        LoopbackNetwork network(server.serverPort());

        FakePlayer player;
        const QStringList texts = sentences(count);
        quint32 seed = 777;
        auto next = [&seed](int n) { seed = seed * 1103515245u + 12345u; return int((seed >> 16) % quint32(n)); };
        qint64 at = 1500;
        for (int i = 0; i < count; ++i) {
            Line line;
            line.text = texts[i];
            line.style = i % 10 == 9 ? 2 : (i % 3 == 1 ? 1 : 0);
            line.duration = line.style == 2 ? 3600 : 1000 + next(3001);
            line.start = at;
            at += line.duration + (i % 4 == 0 ? 0 : 150 + next(400)); // some lines follow at once
            player.lines << line;
        }
        player.setGeometry(QRect(QGuiApplication::primaryScreen()->geometry().topLeft() + QPoint(40, 80), QSize(1000, 560)));
        player.show();
        QVERIFY(QTest::qWaitForWindowExposed(&player));

        MainWindow window;
        window.executable_.clear();
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); window.paddleEngine_ = true; window.applyOcrEngine();
        window.apiKey_ = QStringLiteral("fixture-key:fx"); window.consent_ = true;
        window.queueTransport_ = &network;
        window.setMode(Mode::Video);
        QVERIFY(window.videoMode_);
        VideoMode& video = *window.videoMode_;
        QScreen* screen = player.screen();
        SavedRegion region;
        region.screen = screen; region.screenGeometry = screen->geometry(); region.dpr = screen->devicePixelRatio();
        region.rect = QRect(player.mapToGlobal(player.band().topLeft()) - screen->geometry().topLeft(), player.band().size());
        region.probe = GameWindow::toPhysical(QRect(player.mapToGlobal(QPoint()), player.size()));
        video.setRegion(region);
        video.setWatching(true);
        QVERIFY(video.watching());
        player.raise();
        player.start();
        QTRY_VERIFY_WITH_TIMEOUT(player.finished(), int(at + 30000));
        video.setWatching(false);

        // ---------- evaluation ----------
        int read = 0, falseText = 0, coveredOk = 0;
        QList<qint64> toOcr, toCaption;
        QJsonArray rows;
        for (const Line& line : player.lines) {
            const VideoMode::LineTiming* best = nullptr;
            double bestScore = 0;
            for (const auto& t : video.timings()) {
                const double score = similarity(t.source, line.text);
                // The read must come from the time this line was on screen.
                if (line.shownAt < 0 || t.capturedAt < line.shownAt - 50 || (line.clearedAt >= 0 && t.capturedAt > line.clearedAt + 50)) continue;
                if (score > bestScore) { bestScore = score; best = &t; }
            }
            const bool ok = best && bestScore >= 0.85;
            read += ok;
            QJsonObject row{{"text", line.text}, {"style", line.style}, {"shownAt", line.shownAt}, {"durationMs", line.duration},
                            {"read", ok}, {"similarity", bestScore}};
            if (ok) {
                row.insert("ocrMs", best->ocrDoneAt - line.shownAt);
                if (line.style != 2) toOcr << best->ocrDoneAt - line.shownAt; // word-by-word lines wait on purpose
                if (best->shownAt >= 0) {
                    row.insert("captionMs", best->shownAt - line.shownAt);
                    if (line.style != 2) toCaption << best->shownAt - line.shownAt;
                    coveredOk += best->translation.startsWith(QStringLiteral("[KO] "));
                }
            }
            rows << row;
        }
        for (const auto& t : video.timings()) { // text read while the band was empty
            bool during = false;
            for (const Line& line : player.lines)
                during = during || (line.shownAt >= 0 && t.capturedAt >= line.shownAt - 50 && (line.clearedAt < 0 || t.capturedAt <= line.clearedAt + 120));
            if (!during) ++falseText;
        }
        const double miss = 1.0 - double(read) / count;
        QJsonObject report{{"lines", count}, {"read", read}, {"missRate", miss}, {"falseTextInGaps", falseText},
                           {"captionsShown", coveredOk}, {"translateDelayMs", translateDelay},
                           {"ocrMedianMs", percentile(toOcr, 0.5)}, {"ocrP95Ms", percentile(toOcr, 0.95)},
                           {"captionMedianMs", percentile(toCaption, 0.5)}, {"captionP95Ms", percentile(toCaption, 0.95)},
                           {"requests", int(server.requests.size())}, {"rows", rows}};
        qInfo().noquote() << QJsonDocument(QJsonObject{{"lines", count}, {"read", read}, {"miss", miss}, {"falseText", falseText},
            {"ocrMedian", report["ocrMedianMs"]}, {"ocrP95", report["ocrP95Ms"]}, {"captionMedian", report["captionMedianMs"]},
            {"captionP95", report["captionP95Ms"]}, {"requests", int(server.requests.size())}}).toJson(QJsonDocument::Compact);
        if (!qEnvironmentVariableIsEmpty("SUBTITLE_VIDEO_REPORT")) {
            QFile file(qEnvironmentVariable("SUBTITLE_VIDEO_REPORT"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(report).toJson());
        }
        QVERIFY2(miss <= 0.05, qPrintable(QStringLiteral("missed %1 of %2").arg(count - read).arg(count)));
        QCOMPARE(falseText, 0);
        QVERIFY(coveredOk >= read * 9 / 10); // translations reached the screen while their line was there
        QVERIFY(window.history_.entries().size() >= read);
    }
};
QTEST_MAIN(VideoChecks)
#include "video_checks.moc"
