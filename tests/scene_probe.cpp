// Measurement, not a test: auto translation against a fake game that animates like real ones do
// (blinking advance arrow, moving background, typewriter text). Counts what a line costs: OCR reads,
// translation requests, characters sent, and how long the caption took. The translation server is a
// local echo server, unless --deepl: then the app uses the user's saved DeepL key (P-40 U1, few short
// scenes only; the characters used are reported). Plan and numbers:
// docs/plan/2026-11-15-auto-efficiency.ko.md, docs/plan/2026-11-23-expressions-and-open-checks.ko.md.
//
//   scene_probe.exe <result.json> [--deepl] [scene name ...]
#include "main_window.h"
#include "credential_store.h"
#include "translation_service.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
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
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

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
    // Fade-in text (common in visual novels): each character goes from clear to white over fadeMs
    // after it appears. 0 = characters appear at once.
    int fadeMs = 0;
    void showLine(const QString& text, bool typed) {
        line = text; shown = typed ? 0 : int(text.size());
        revealedAt_ = QList<qint64>(shown, clock_.elapsed());
    }
    void revealNext() { ++shown; revealedAt_ << clock_.elapsed(); }

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
        const QRect textBox = box.adjusted(34, 20, -70, -20);
        if (fadeMs <= 0) {
            p.drawText(textBox, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, line.left(shown));
        } else {
            // One line, character by character, each at its own opacity.
            const QFontMetrics metrics(font);
            const int baseline = textBox.center().y() + (metrics.ascent() - metrics.descent()) / 2;
            for (int i = 0; i < shown && i < line.size(); ++i) {
                const qint64 age = i < revealedAt_.size() ? t - revealedAt_[i] : fadeMs;
                const int alpha = int(255 * std::clamp(double(age) / fadeMs, 0.0, 1.0));
                p.setPen(QColor(255, 255, 255, alpha));
                p.drawText(textBox.left() + metrics.horizontalAdvance(line.left(i)), baseline, line.mid(i, 1));
            }
        }
        if (sparkles) {
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 10; ++i) {
                const double x = fmod(i * 131.0 + t * (0.05 + i * 0.006), width() + 40.0) - 20;
                const double y = 30 + fmod(i * 41.0 + t * 0.02 * (i % 2 + 1), height() - 60.0);
                p.setBrush(QColor(255, 255, 255, 150));
                p.drawEllipse(QPointF(x, y), 4 + i % 3 * 2, 4 + i % 3 * 2);
            }
        }
        if (arrow != NoArrow && shown == line.size() && !line.isEmpty() &&
            (fadeMs <= 0 || revealedAt_.isEmpty() || t - revealedAt_.last() >= fadeMs)) {
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
    QList<qint64> revealedAt_; // when each shown character appeared
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
    bool numbered = false; // every line different ("12回目、..."), so none is answered from memory
    bool onlyWhenNamed = false; // long runs: not part of the default set
    bool varied = false;   // every line a different sentence (720 of them), so each one is translated
    int fadeMs = 0;        // characters fade in over this long (visual-novel text)
};

// 24 people x 30 places = 720 different sentences: "先生は公園で本を読みました。"
QString variedLine(int i) {
    static const QStringList who = {
        "私", "先生", "友達", "母", "父", "兄", "姉", "弟", "妹", "猫", "犬", "店員",
        "医者", "子供", "彼", "彼女", "隣の人", "社長", "学生", "祖母", "祖父", "警察官", "運転手", "歌手"};
    static const QStringList where = {
        "駅で", "公園で", "学校で", "図書館で", "病院で", "会社で", "海で", "山で", "空港で", "喫茶店で",
        "映画館で", "台所で", "庭で", "部屋で", "教室で", "市場で", "橋の上で", "森の中で", "川のそばで", "町で",
        "港で", "銀行で", "郵便局で", "美術館で", "動物園で", "神社で", "店で", "車の中で", "電車で", "家で"};
    static const QStringList did = {"本を読みました", "歌を歌いました", "写真を撮りました", "手紙を書きました",
                                    "ご飯を食べました", "友達を待ちました"};
    return who[i % who.size()] + QStringLiteral("は") + where[(i / who.size()) % where.size()] + did[i % did.size()];
}
}

// --deepl: the saved key, and whether it is a free-plan key.
QString realKey;
bool realFree = false;

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
        game.fadeMs = scene.fadeMs;
        game.show();
        wait(400);
        EchoHttp http;
        MainWindow window;
        window.paddleDir_ = qEnvironmentVariable("SUBTITLE_TEST_PADDLE");
        window.paddleEngine_ = true;
        window.applyOcrEngine();
        window.ocrLanguageBox_->setCurrentIndex(window.ocrLanguageBox_->findData("jpn"));
        const bool real = !realKey.isEmpty();
        window.apiKey_ = real ? realKey : QStringLiteral("test-key:fx");
        window.consent_ = true;
        window.freeApi_ = real ? realFree : true;
        if (!real) window.translation_.setEndpointForTests(http.url());
        window.followGame_->setChecked(false);
        window.ocr_.waitForWarmUp();
        int reads = 0;
        double readMs = 0, findMs = 0, recognizeMs = 0;
        const bool verbose = !qEnvironmentVariableIsEmpty("SCENE_PROBE_VERBOSE");
        QElapsedTimer sceneClock;
        sceneClock.start();
        connect(&window.ocr_, &OcrService::recognized, this, [&, verbose](const QString& text) {
            ++reads;
            const PaddleOcr::Result& details = window.ocr_.paddleDetails();
            readMs += double(window.ocr_.elapsedMs()); findMs += details.detectMs; recognizeMs += details.recognizeMs;
            double lowest = 1;
            for (const auto& line : details.lines) lowest = std::min(lowest, line.lowestLetter); // what doubtful() uses
            if (verbose) std::printf("  %6lld read  [%s] %lld ms (find %.0f, read %.0f) lowest letter %.3f\n", sceneClock.elapsed(),
                                     qPrintable(QString(text).replace(QLatin1Char('\n'), QLatin1Char('|'))),
                                     window.ocr_.elapsedMs(), details.detectMs, details.recognizeMs, lowest);
        });
        connect(&window.ocr_, &OcrService::failed, this, [&, verbose](const QString& error) {
            ++reads;
            if (verbose) std::printf("  %6lld fail  [%s]\n", sceneClock.elapsed(), qPrintable(error.left(60)));
        });
        window.setRegion(game.screen(), QRect(game.mapToGlobal(QPoint()) - game.screen()->geometry().topLeft(), game.size()));
        // Test mode never opens a DeepL connection ahead of a line; the app does on every poll (no text, no quota).
        if (real) {
            window.translation_.warmUp(window.freeApi_);
            connect(window.autoTimer_, &QTimer::timeout, &window, [&window] { window.translation_.warmUp(window.freeApi_); });
        }
        window.setAutoTranslate(true);
        if (const int poll = qEnvironmentVariableIntValue("SCENE_PROBE_POLL")) window.autoTimer_->setInterval(poll);
        wait(600);
        int polls = 0;
        connect(window.autoTimer_, &QTimer::timeout, this, [&polls] { ++polls; });
        const qint64 cpuBefore = processCpuMs();
        QElapsedTimer wall;
        wall.start();

        QJsonArray latencies;
        int captioned = 0;
        qint64 latencySum = 0, latencyMax = 0;
        qsizetype lineCharacters = 0;
        QString previousCaption; // real DeepL answers in Korean: a line is captioned when a new caption shows for it
        QStringList source = scene.typeMs ? twoSentences : oneSentence;
        if (scene.varied) { source.clear(); for (int i = 0; i < scene.lines; ++i) source << variedLine(i); }
        QJsonArray memory; // private bytes in MB after each line (P-40 U3)
        QJsonArray handles, gdi, user; // kernel handles and GDI/USER objects after each line (P-12 C-06)
        const double memoryStart = privateMb();
        for (int i = 0; i < scene.lines; ++i) {
            const QString text = (scene.numbered ? QStringLiteral("%1回目、").arg(i + 1) : QString()) +
                                 source[i % source.size()] + (scene.punctuated ? QStringLiteral("。") : QString());
            lineCharacters += text.size();
            game.showLine(text, scene.typeMs > 0);
            if (verbose) std::printf("  %6lld show  [%s]\n", sceneClock.elapsed(), qPrintable(text));
            while (game.shown < text.size()) { wait(scene.typeMs); game.revealNext(); }
            QElapsedTimer clock;
            clock.start();
            qint64 latency = -1;
            while (clock.elapsed() < scene.holdMs) {
                wait(15);
                if (latency >= 0) continue;
                const QString shown = bare(window.result_->toPlainText());
                if (real) {
                    const QString read = bare(window.original_->toPlainText());
                    if (!shown.isEmpty() && shown != previousCaption && !window.busy_ && editDistance(read, bare(text)) * 5 <= text.size())
                        latency = clock.elapsed();
                } else if (!shown.isEmpty() && editDistance(shown, bare(text)) * 5 <= text.size()) latency = clock.elapsed();
            }
            previousCaption = bare(window.result_->toPlainText());
            if (verbose) std::printf("  %6lld line %d latency %lld status [%s] shown [%s]\n", sceneClock.elapsed(), i, latency,
                                     qPrintable(window.status_->text().left(50).replace(QLatin1Char('\n'), QLatin1Char(' '))),
                                     qPrintable(window.result_->toPlainText().left(40)));
            latencies << latency;
            if (latency >= 0) { ++captioned; latencySum += latency; latencyMax = std::max(latencyMax, latency); }
            memory << privateMb();
            DWORD handleCount = 0;
            GetProcessHandleCount(GetCurrentProcess(), &handleCount);
            handles << int(handleCount);
            gdi << int(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
            user << int(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
            const int every = scene.varied ? 60 : 10;
            if ((scene.numbered || scene.varied) && (i + 1) % every == 0)
                std::printf("  line %d (%lld s): %.1f MB, handles %d, GDI %d, USER %d, cpu %.1f%%\n", i + 1, sceneClock.elapsed() / 1000,
                            privateMb(), int(handleCount), gdi.last().toInt(), user.last().toInt(),
                            100.0 * double(processCpuMs() - cpuBefore) / double(std::max<qint64>(1, wall.elapsed())));
            std::fflush(stdout);
        }
        const double cpuPercent = 100.0 * double(processCpuMs() - cpuBefore) / double(std::max<qint64>(1, wall.elapsed()));
        window.setAutoTranslate(false);
        window.resultWindow_->hide();
        QStringList distinct = http.texts;
        distinct.removeDuplicates();
        if (real) {
            QJsonArray captions;
            for (const auto& entry : window.backlog_) captions << QJsonObject{{"source", entry.source}, {"translation", entry.translation}};
            return QJsonObject{
                {"scene", scene.name}, {"deepl", true}, {"memoryStartMb", memoryStart}, {"memoryMb", memory}, {"lines", scene.lines}, {"lineCharacters", lineCharacters},
                {"captioned", captioned}, {"latencyMs", latencies},
                {"latencyAvgMs", captioned ? latencySum / captioned : -1}, {"latencyMaxMs", latencyMax},
                {"ocrReads", reads}, {"requests", window.sentRequests_}, {"textsSent", window.sentRequests_},
                {"distinctTexts", window.sentRequests_}, {"charactersSent", window.sentCharacters_},
                {"cpuPercentOfOneCore", cpuPercent}, {"polls", polls}, {"captions", captions}};
        }
        // Only the fake game's own text is written down: when another window covers the region, what it
        // shows is the user's and is recorded as "(다른 글)" without its content.
        QStringList sent;
        int otherTexts = 0;
        for (const QString& t : http.texts) {
            QString piece = bare(t);
            if (piece.endsWith(QChar(u'。'))) piece.chop(1);
            const bool own = std::any_of(source.begin(), source.end(), [&](const QString& line) {
                return editDistance(piece.right(line.size()), line) * 4 <= line.size(); // "12回目、" is not sent
            });
            sent << (own ? t : QStringLiteral("(다른 글)"));
            otherTexts += !own;
        }
        return QJsonObject{
            {"scene", scene.name}, {"lines", scene.lines}, {"lineCharacters", lineCharacters},
            {"captioned", captioned}, {"latencyMs", latencies},
            {"latencyAvgMs", captioned ? latencySum / captioned : -1}, {"latencyMaxMs", latencyMax},
            {"ocrReads", reads}, {"readAvgMs", reads ? readMs / reads : 0}, {"findAvgMs", reads ? findMs / reads : 0},
            {"recognizeAvgMs", reads ? recognizeMs / reads : 0}, {"requests", http.requests}, {"textsSent", http.texts.size()},
            {"distinctTexts", distinct.size()}, {"charactersSent", http.characters()},
            {"cpuPercentOfOneCore", cpuPercent}, {"polls", polls}, {"sent", QJsonArray::fromStringList(sent)},
            {"otherTexts", otherTexts}, {"memoryStartMb", memoryStart}, {"memoryMb", memory},
            {"handles", handles}, {"gdiObjects", gdi}, {"userObjects", user}};
    }

private:
    // Committed private memory of this process, in MB.
    static double privateMb() {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return 0;
        return double(counters.PrivateUsage) / (1024.0 * 1024.0);
    }
    // Kernel and user time of this process, fake game and probe loop included.
    static qint64 processCpuMs() {
        FILETIME created, exited, kernel, user;
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
        const auto ticks = [](const FILETIME& t) { return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        return qint64((ticks(kernel) + ticks(user)) / 10000);
    }
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
        {"idle-still", FakeGame::NoArrow, false, false, 0, true, 1, 20000},
        {"idle-blink", FakeGame::Blink, false, false, 0, true, 1, 20000},
        {"idle-moving", FakeGame::Bounce, true, false, 0, true, 1, 20000},
        {"idle-sparkle", FakeGame::Blink, true, true, 0, true, 1, 30000},
        // Visual-novel text: typed out, each character fading in over 250 ms (P-12 follow-up).
        {"faded", FakeGame::Blink, false, false, 60, true, 4, 4000, false, true, false, 250},
        {"faded-moving", FakeGame::Bounce, true, false, 60, true, 4, 4000, false, true, false, 250},
        {"faded-slow", FakeGame::Blink, false, false, 120, true, 4, 5000, false, true, false, 400},
        // 60 different lines, 10 s each, over the busiest picture: memory and CPU over 10 minutes.
        {"long", FakeGame::Blink, true, true, 0, true, 60, 10000, true, true},
        // P-12 C-06: an hour of auto translation, moving background, a new sentence every 5 s.
        {"hour", FakeGame::Blink, true, false, 0, true, 720, 5000, false, true, true},
        // P-12 C-03: auto translation over a still line for 5 minutes.
        {"idle5", FakeGame::NoArrow, false, false, 0, true, 1, 300000, false, true},
    };
    QStringList wanted;
    bool deepl = false;
    for (int i = 2; i < argc; ++i) {
        if (QLatin1String(argv[i]) == QLatin1String("--deepl")) deepl = true;
        else wanted << QString::fromLocal8Bit(argv[i]);
    }
    TranslationService usage;
    qint64 usedBefore = -1, usedAfter = -1, limit = -1;
    auto checkUsage = [&](qint64& into) {
        QEventLoop loop;
        auto got = QObject::connect(&usage, &TranslationService::usageChecked, &loop, [&](qint64 used, qint64 max) { into = used; limit = max; loop.quit(); });
        auto bad = QObject::connect(&usage, &TranslationService::failed, &loop, &QEventLoop::quit);
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        usage.checkUsage(realKey, realFree);
        loop.exec();
        QObject::disconnect(got); QObject::disconnect(bad);
    };
    if (deepl) {
        // Only scenes named on the command line: real DeepL costs the user's characters.
        if (wanted.isEmpty()) { std::puts("--deepl needs scene names"); return 2; }
        QString error;
        realKey = Credentials::load(error);
        if (realKey.isEmpty()) { std::printf("no DeepL key: %s\n", qPrintable(error)); return 3; }
        realFree = realKey.endsWith(QLatin1String(":fx"));
        checkUsage(usedBefore);
    }
    WindowChecks probe;
    QJsonArray results;
    std::printf("%-14s %5s %8s %8s %8s %6s %8s %6s %6s %6s %6s\n", "scene", "lines", "caption", "avg ms", "max ms", "reads", "requests", "texts", "chars", "cpu%", "polls");
    for (const Scene& scene : scenes) {
        if (wanted.isEmpty() ? scene.onlyWhenNamed : !wanted.contains(QLatin1String(scene.name))) continue;
        const QJsonObject r = probe.run(scene);
        results << r;
        std::printf("%-14s %5d %8d %8d %8d %6d %8d %6d %6d %6.1f %6d (line chars %d)\n", scene.name, r["lines"].toInt(),
                    r["captioned"].toInt(), r["latencyAvgMs"].toInt(), r["latencyMaxMs"].toInt(), r["ocrReads"].toInt(),
                    r["requests"].toInt(), r["textsSent"].toInt(), r["charactersSent"].toInt(), r["cpuPercentOfOneCore"].toDouble(),
                    r["polls"].toInt(), r["lineCharacters"].toInt());
        std::fflush(stdout);
    }
    QSettings().clear();
    if (deepl) {
        QEventLoop pause; QTimer::singleShot(1500, &pause, &QEventLoop::quit); pause.exec(); // the count trails a moment behind
        checkUsage(usedAfter);
        std::printf("DeepL usage %lld -> %lld of %lld\n", usedBefore, usedAfter, limit);
        for (const auto& value : results) {
            const QJsonObject r = value.toObject();
            for (const auto& caption : r["captions"].toArray())
                std::printf("  %s: %s -> %s\n", qPrintable(r["scene"].toString()), qPrintable(caption.toObject()["source"].toString()),
                            qPrintable(caption.toObject()["translation"].toString()));
        }
        results << QJsonObject{{"usedBefore", usedBefore}, {"usedAfter", usedAfter}, {"limit", limit}};
    }
    if (argc > 1) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(results).toJson());
    }
    return 0;
}
