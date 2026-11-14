// Measurement, not a test: auto translation against a fake game that animates like real ones do
// (blinking advance arrow, moving background, typewriter text). Counts what a line costs: OCR reads,
// translation requests, characters sent, and how long the caption took. The translation server is a
// local echo server, never DeepL. Plan and numbers: docs/plan/2026-11-15-auto-efficiency.ko.md.
//
//   scene_probe.exe <result.json> [scene name ...]
#include "main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
// Answers every translate request with one "KO<source>" per text, like a server that always works.
class EchoHttp final : public QObject {
public:
    QStringList texts; // every text sent, in order
    int requests = 0;
    EchoHttp() {
        server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) serve(socket);
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort())); }
    qsizetype characters() const { qsizetype n = 0; for (const QString& t : texts) n += t.size(); return n; }

private:
    void serve(QTcpSocket* socket) {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            *buffer += socket->readAll();
            const qsizetype headEnd = buffer->indexOf("\r\n\r\n");
            if (headEnd < 0) return;
            qsizetype length = 0;
            for (const QByteArray& line : buffer->left(headEnd).split('\n'))
                if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headEnd + 4 + length) return;
            const QByteArray head = buffer->left(headEnd);
            const QJsonObject body = QJsonDocument::fromJson(buffer->mid(headEnd + 4)).object();
            buffer->clear();
            QByteArray reply = "{}";
            if (head.startsWith("POST")) {
                ++requests;
                QJsonArray translations;
                for (const auto& text : body.value("text").toArray()) {
                    texts << text.toString();
                    translations << QJsonObject{{"detected_source_language", "JA"}, {"text", "KO<" + text.toString() + ">"}};
                }
                reply = QJsonDocument(QJsonObject{{"translations", translations}}).toJson(QJsonDocument::Compact);
            } else {
                reply = "{\"character_count\":0,\"character_limit\":500000}";
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply);
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
    QTcpServer server_;
};

// A dialogue box as games draw it. Everything is inside the area the user would select.
class FakeGame final : public QWidget {
public:
    enum Arrow { NoArrow, Blink, Bounce };
    Arrow arrow = NoArrow;
    bool movingBackground = false;
    bool sparkles = false; // lights that drift in front of the text, as weather and effects do
    QString line;
    int shown = 0;

    FakeGame() {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        resize(1100, 220);
        move(30, 30);
        frame_.setInterval(33);
        connect(&frame_, &QTimer::timeout, this, [this] { update(); });
        frame_.start();
        clock_.start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qint64 t = clock_.elapsed();
        if (movingBackground) {
            // A scene behind a translucent box: a slow pan and drifting lights.
            QLinearGradient sky(0, 0, width(), height());
            const int shift = int(40 + 30 * qSin(t / 900.0));
            sky.setColorAt(0, QColor(40 + shift, 70, 120));
            sky.setColorAt(1, QColor(20, 40 + shift, 90));
            p.fillRect(rect(), sky);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 14; ++i) {
                const double x = fmod(i * 97.0 + t * (0.03 + i * 0.004), width() + 60.0) - 30;
                const double y = 20 + fmod(i * 53.0 + t * 0.012 * (i % 3 + 1), height() - 40.0);
                p.setBrush(QColor(255, 240, 200, 70));
                p.drawEllipse(QPointF(x, y), 9 + i % 4 * 3, 9 + i % 4 * 3);
            }
        } else {
            p.fillRect(rect(), QColor(24, 28, 48));
        }
        const QRect box = rect().adjusted(16, 16, -16, -16);
        p.setPen(QPen(QColor(230, 230, 240), 2));
        p.setBrush(QColor(0, 0, 0, movingBackground ? 165 : 255));
        p.drawRoundedRect(box, 14, 14);
        QFont font(QStringLiteral("Yu Gothic"));
        font.setPixelSize(40);
        font.setBold(true);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(box.adjusted(34, 20, -70, -20), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, line.left(shown));
        if (sparkles) {
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 10; ++i) {
                const double x = fmod(i * 131.0 + t * (0.05 + i * 0.006), width() + 40.0) - 20;
                const double y = 30 + fmod(i * 41.0 + t * 0.02 * (i % 2 + 1), height() - 60.0);
                p.setBrush(QColor(255, 255, 255, 150));
                p.drawEllipse(QPointF(x, y), 4 + i % 3 * 2, 4 + i % 3 * 2);
            }
        }
        if (arrow != NoArrow && shown == line.size() && !line.isEmpty()) {
            const bool on = arrow == Bounce || (t / 450) % 2 == 0;
            if (on) {
                const double dy = arrow == Bounce ? 6 * qSin(t / 160.0) : 0;
                QPolygonF triangle;
                const QPointF at(box.right() - 44, box.bottom() - 40 + dy);
                triangle << at << at + QPointF(26, 0) << at + QPointF(13, 18);
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                p.drawPolygon(triangle);
            }
        }
    }

private:
    QTimer frame_;
    QElapsedTimer clock_;
};

int editDistance(const QString& a, const QString& b) {
    QList<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}
QString bare(QString text) {
    text.remove(QStringLiteral("KO<")).remove(QLatin1Char('>'));
    text.remove(QLatin1Char(' ')).remove(QLatin1Char('\n'));
    return text;
}

struct Scene {
    const char* name;
    FakeGame::Arrow arrow;
    bool movingBackground;
    bool sparkles;
    int typeMs;       // 0 = the line appears at once
    bool punctuated;
    int lines;
    int holdMs;       // time each line stays after it is complete
};
}

// Named like the UI test class: MainWindow lets exactly that class read its members.
class WindowChecks final : public QObject {
public:
    QJsonObject run(const Scene& scene) {
        static const QStringList oneSentence = {
            QStringLiteral("今日はいい天気ですね"), QStringLiteral("明日は学校に行きます"),
            QStringLiteral("この本はとても面白い"), QStringLiteral("駅まで一緒に歩きましょう")};
        static const QStringList twoSentences = {
            QStringLiteral("今日はいい天気ですね。散歩に行きましょう"), QStringLiteral("明日は雨が降ります。傘を持って行きます"),
            QStringLiteral("この本は面白いです。もう一度読みたい"), QStringLiteral("駅まで歩きましょう。電車に乗ります")};
        QSettings().clear();
        QSettings().setValue("display/inlineCaptions", true);
        FakeGame game;
        game.arrow = scene.arrow;
        game.movingBackground = scene.movingBackground;
        game.sparkles = scene.sparkles;
        game.show();
        wait(400);
        EchoHttp http;
        MainWindow window;
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE");
        window.paddleEngine_ = true;
        window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        window.apiKey_ = QStringLiteral("test-key:fx");
        window.consent_ = true;
        window.freeApi_ = true;
        window.translation_.setEndpointForTests(http.url());
        window.followGame_->setChecked(false);
        window.ocr_.waitForWarmUp();
        int reads = 0;
        const bool verbose = !qEnvironmentVariableIsEmpty("SCENE_PROBE_VERBOSE");
        QElapsedTimer sceneClock;
        sceneClock.start();
        connect(&window.ocr_, &OcrService::recognized, this, [&, verbose](const QString& text) {
            ++reads;
            if (verbose) std::printf("  %6lld read  [%s]\n", sceneClock.elapsed(), qPrintable(QString(text).replace(QLatin1Char('\n'), QLatin1Char('|'))));
        });
        connect(&window.ocr_, &OcrService::failed, this, [&, verbose](const QString& error) {
            ++reads;
            if (verbose) std::printf("  %6lld fail  [%s]\n", sceneClock.elapsed(), qPrintable(error.left(60)));
        });
        window.setRegion(game.screen(), QRect(game.mapToGlobal(QPoint()) - game.screen()->geometry().topLeft(), game.size()));
        window.setAutoTranslate(true);
        wait(600);

        QJsonArray latencies;
        int captioned = 0;
        qint64 latencySum = 0, latencyMax = 0;
        qsizetype lineCharacters = 0;
        const QStringList& source = scene.typeMs ? twoSentences : oneSentence;
        for (int i = 0; i < scene.lines; ++i) {
            const QString text = source[i % source.size()] + (scene.punctuated ? QStringLiteral("。") : QString());
            lineCharacters += text.size();
            game.line = text;
            game.shown = scene.typeMs ? 0 : int(text.size());
            if (verbose) std::printf("  %6lld show  [%s]\n", sceneClock.elapsed(), qPrintable(text));
            while (game.shown < text.size()) { wait(scene.typeMs); ++game.shown; }
            QElapsedTimer clock;
            clock.start();
            qint64 latency = -1;
            while (clock.elapsed() < scene.holdMs) {
                wait(15);
                if (latency >= 0) continue;
                const QString shown = bare(window.result_->toPlainText());
                if (!shown.isEmpty() && editDistance(shown, bare(text)) * 5 <= text.size()) latency = clock.elapsed();
            }
            if (verbose) std::printf("  %6lld line %d latency %lld status [%s] shown [%s]\n", sceneClock.elapsed(), i, latency,
                                     qPrintable(window.status_->text().left(50).replace(QLatin1Char('\n'), QLatin1Char(' '))),
                                     qPrintable(window.result_->toPlainText().left(40)));
            latencies << latency;
            if (latency >= 0) { ++captioned; latencySum += latency; latencyMax = std::max(latencyMax, latency); }
        }
        window.setAutoTranslate(false);
        window.resultWindow_->hide();
        QStringList distinct = http.texts;
        distinct.removeDuplicates();
        return QJsonObject{
            {"scene", scene.name}, {"lines", scene.lines}, {"lineCharacters", lineCharacters},
            {"captioned", captioned}, {"latencyMs", latencies},
            {"latencyAvgMs", captioned ? latencySum / captioned : -1}, {"latencyMaxMs", latencyMax},
            {"ocrReads", reads}, {"requests", http.requests}, {"textsSent", http.texts.size()},
            {"distinctTexts", distinct.size()}, {"charactersSent", http.characters()},
            {"sent", QJsonArray::fromStringList(http.texts)}};
    }

private:
    static void wait(int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setProperty("testMode", true);
    app.setOrganizationName("SubtitleTranslatorChecks");
    app.setApplicationName("TemporarySceneProbe");
    app.setQuitOnLastWindowClosed(false);
    if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) { std::puts("SUBTITLE_TEST_PADDLE is not set"); return 2; }
    const Scene scenes[] = {
        {"still", FakeGame::NoArrow, false, false, 0, true, 4, 4000},
        {"still-nomark", FakeGame::NoArrow, false, false, 0, false, 4, 4000},
        {"blink", FakeGame::Blink, false, false, 0, true, 4, 4000},
        {"blink-nomark", FakeGame::Blink, false, false, 0, false, 4, 4000},
        {"bounce", FakeGame::Bounce, false, false, 0, true, 4, 4000},
        {"bounce-nomark", FakeGame::Bounce, false, false, 0, false, 4, 4000},
        {"moving", FakeGame::NoArrow, true, false, 0, true, 4, 4000},
        {"moving-nomark", FakeGame::NoArrow, true, false, 0, false, 4, 4000},
        {"sparkle", FakeGame::Blink, true, true, 0, true, 4, 6000},
        {"sparkle-nomark", FakeGame::Blink, true, true, 0, false, 4, 6000},
        {"typed", FakeGame::NoArrow, false, false, 60, true, 4, 4000},
        {"typed-blink", FakeGame::Blink, false, false, 60, true, 4, 4000},
        {"typed-moving", FakeGame::Bounce, true, false, 60, true, 4, 4000},
        {"idle-blink", FakeGame::Blink, false, false, 0, true, 1, 20000},
        {"idle-moving", FakeGame::Bounce, true, false, 0, true, 1, 20000},
        {"idle-sparkle", FakeGame::Blink, true, true, 0, true, 1, 30000},
    };
    QStringList wanted;
    for (int i = 2; i < argc; ++i) wanted << QString::fromLocal8Bit(argv[i]);
    WindowChecks probe;
    QJsonArray results;
    std::printf("%-14s %5s %8s %8s %8s %6s %8s %6s %6s\n", "scene", "lines", "caption", "avg ms", "max ms", "reads", "requests", "texts", "chars");
    for (const Scene& scene : scenes) {
        if (!wanted.isEmpty() && !wanted.contains(QLatin1String(scene.name))) continue;
        const QJsonObject r = probe.run(scene);
        results << r;
        std::printf("%-14s %5d %8d %8d %8d %6d %8d %6d %6d (line chars %d)\n", scene.name, r["lines"].toInt(),
                    r["captioned"].toInt(), r["latencyAvgMs"].toInt(), r["latencyMaxMs"].toInt(), r["ocrReads"].toInt(),
                    r["requests"].toInt(), r["textsSent"].toInt(), r["charactersSent"].toInt(), r["lineCharacters"].toInt());
        std::fflush(stdout);
    }
    QSettings().clear();
    if (argc > 1) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(results).toJson());
    }
    return 0;
}
