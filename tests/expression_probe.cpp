// Measurement, not a test (P-40): everyday kanji, each country's emoticons, emoji, punctuation and
// interjections drawn with real fonts, read by PaddleOCR, and put through the app's own rules:
// which part is sent for translation, whether the line counts as finished, how the line is
// compared. With --deepl the lines are also sent to the real DeepL with the user's key, exactly
// as the app splits them, and the characters used are reported. Plan and results:
// docs/plan/2026-11-23-expressions-and-open-checks.ko.md
//
//   expression_probe.exe <result.json> [--deepl]
#include "main_window.h"
#include "credential_store.h"
#include "languages.h"
#include "paddle_ocr.h"
#include "translation_service.h"
#include "subtitle/text.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstdio>

namespace {
struct Sample {
    const char* category; // kanji, emoticon, emoji, punctuation, interjection
    const char* language; // ja, ko, zh, en
    QString text;
    QStringList marks;    // parts that must survive reading (an emoticon, "！？" ...)
};

QList<Sample> samples() {
    auto s = [](const char* c, const char* l, const char* t, std::initializer_list<const char*> m = {}) {
        QStringList marks;
        for (const char* mark : m) marks << QString::fromUtf8(mark);
        return Sample{c, l, QString::fromUtf8(t), marks};
    };
    return {
        // Japanese: everyday kanji
        s("kanji", "ja", "今日は天気がいいですね。"), s("kanji", "ja", "明日、駅で友達と会います。"),
        s("kanji", "ja", "先生、大丈夫ですか？"), s("kanji", "ja", "会社に行く時間です。"),
        s("kanji", "ja", "病院で薬をもらった。"), s("kanji", "ja", "電車が遅れています。"),
        s("kanji", "ja", "晩ご飯を食べましょう。"), s("kanji", "ja", "誕生日おめでとう！"),
        // Japanese: emoticons and net slang
        s("emoticon", "ja", "ありがとう(^_^)", {"(^_^)"}), s("emoticon", "ja", "よろしくね(^^)", {"(^^)"}),
        s("emoticon", "ja", "疲れた…(´・ω・`)", {"(´・ω・`)"}), s("emoticon", "ja", "泣きそう(T_T)", {"(T_T)"}),
        s("emoticon", "ja", "やったー＼(^o^)／", {"＼(^o^)／"}), s("emoticon", "ja", "それな ｗｗｗ", {"ｗｗｗ"}),
        s("emoticon", "ja", "面白すぎ（笑）", {"（笑）"}), s("emoticon", "ja", "負けた orz", {"orz"}),
        // Japanese: emoji
        s("emoji", "ja", "おはよう😊", {"😊"}), s("emoji", "ja", "最高！👍", {"👍"}),
        s("emoji", "ja", "おめでとう🎉", {"🎉"}), s("emoji", "ja", "悲しい😭", {"😭"}),
        // Japanese: punctuation and interjections
        s("punctuation", "ja", "えっ！？", {"！？"}), s("punctuation", "ja", "うわあああ！！", {"！！"}),
        s("punctuation", "ja", "……", {"……"}), s("punctuation", "ja", "そうなんだ・・・", {"・・・"}),
        s("punctuation", "ja", "まあね～", {"～"}), s("punctuation", "ja", "ふふっ♪", {"♪"}),
        s("punctuation", "ja", "好きだよ♡", {"♡"}), s("punctuation", "ja", "すごい☆", {"☆"}),
        s("punctuation", "ja", "ええええ⁉", {"⁉"}), s("interjection", "ja", "は？", {"？"}),
        s("interjection", "ja", "あはは"), s("interjection", "ja", "うーん"),
        // Real brackets that the lone-"」" rule must leave alone
        s("punctuation", "ja", "「本当に？」", {"「", "」"}), s("punctuation", "ja", "『ありがとう』", {"『", "』"}),
        s("punctuation", "ja", "まだ終わってない」", {"」"}),
        // Korean
        s("emoticon", "ko", "오늘 날씨 좋다 ㅋㅋㅋ", {"ㅋㅋㅋ"}), s("emoticon", "ko", "진짜 슬프다 ㅠㅠ", {"ㅠㅠ"}),
        s("emoticon", "ko", "고마워요^^", {"^^"}), s("emoticon", "ko", "배고파 ㅎㅎ", {"ㅎㅎ"}),
        s("interjection", "ko", "헐 대박!!", {"!!"}), s("punctuation", "ko", "아… 그렇구나", {"…"}),
        s("punctuation", "ko", "와아~ 신난다!", {"~"}), s("emoji", "ko", "사랑해💕", {"💕"}),
        s("kanji", "ko", "학교(學校)에 갑니다", {"學校"}), s("kanji", "ko", "대한민국 만세!"),
        // Chinese (simplified)
        s("kanji", "zh", "今天天气很好。"), s("kanji", "zh", "谢谢你！"), s("kanji", "zh", "没关系，我来吧。"),
        s("interjection", "zh", "哈哈哈，太好笑了"), s("emoji", "zh", "加油！💪", {"💪"}),
        s("emoticon", "zh", "我太难了(╥﹏╥)", {"(╥﹏╥)"}), s("emoticon", "zh", "这操作666", {"666"}),
        s("punctuation", "zh", "你在干嘛？？", {"？？"}),
        // English
        s("emoticon", "en", "See you tomorrow :)", {":)"}), s("interjection", "en", "LOL that's so funny!!", {"!!"}),
        s("punctuation", "en", "Oh no... :(", {"...", ":("}), s("emoticon", "en", "Thanks! <3", {"<3"}),
        s("emoji", "en", "Wow!!! 😂", {"😂"}),
    };
}

QStringList fontsFor(const QString& language) {
    if (language == "ja") return {"Yu Gothic", "Meiryo", "MS Gothic"};
    if (language == "ko") return {"Malgun Gothic"};
    if (language == "zh") return {"Microsoft YaHei"};
    return {"Segoe UI"};
}

// plain: black on white. game: bold white text with a dark outline on a dark box, as dialogue boxes do.
QImage draw(const QString& text, const QString& family, bool game) {
    QImage image(1100, 130, QImage::Format_RGB32);
    image.fill(game ? QColor(28, 32, 56) : Qt::white);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font(family);
    font.setPixelSize(44);
    font.setBold(game);
    p.setFont(font);
    if (!game) {
        p.setPen(Qt::black);
        p.drawText(image.rect().adjusted(30, 0, -30, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        return image;
    }
    QPainterPath path;
    const int baseline = (image.height() + QFontMetrics(font).ascent() - QFontMetrics(font).descent()) / 2;
    path.addText(30, baseline, font, text);
    p.setPen(QPen(QColor(10, 10, 20), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.fillPath(path, Qt::white);
    return image;
}

QString compact(QString s) {
    s = s.normalized(QString::NormalizationForm_KC);
    s.remove(QRegularExpression(QStringLiteral("\\s")));
    return s;
}
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
}

// Named like the UI test class: MainWindow lets exactly that class use its rules.
class WindowChecks final {
public:
    static bool finished(const QString& text) { return MainWindow::looksFinished(text); }
    static QString key(const QString& text) { return MainWindow::lineKey(text); }
};

namespace {
// What the app would send for this line: the pieces and DeepL source language, as MainWindow does.
struct Split { QStringList sent, kept; QString source; };
Split split(const QString& text, const QString& target, const QString& ocr) {
    const auto keptScripts = subtitle::preservableScripts(target.section('-', 0, 0).toLower().toStdString(),
                                                          Languages::deeplSource(ocr).toLower().toStdString());
    Split out;
    unsigned scripts = 0;
    for (const auto& segment : subtitle::splitForTranslation(text.toStdString(), keptScripts)) {
        const QString part = QString::fromStdString(segment.utf8);
        if (segment.translate) { out.sent << part; scripts |= subtitle::scriptsIn(segment.utf8); }
        else out.kept << part;
    }
    out.source = Languages::sourceForScripts(scripts, ocr);
    return out;
}
QString targetFor(const QString& language) { return language == "ko" ? QStringLiteral("JA") : QStringLiteral("KO"); }

void wait(TranslationService& service) {
    QEventLoop loop;
    QObject::connect(&service, &TranslationService::translated, &loop, &QEventLoop::quit);
    QObject::connect(&service, &TranslationService::failed, &loop, &QEventLoop::quit);
    QObject::connect(&service, &TranslationService::usageChecked, &loop, &QEventLoop::quit);
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString models = qEnvironmentVariable("SUBTITLE_TEST_PADDLE");
    if (models.isEmpty() || argc < 2) { std::puts("usage: SUBTITLE_TEST_PADDLE=<models> expression_probe <result.json> [--deepl]"); return 2; }
    const bool deepl = argc > 2 && QString::fromLocal8Bit(argv[2]) == QLatin1String("--deepl");
    const QString ocrCode = QString::fromLatin1(Languages::DefaultOcr); // what users run: auto-detect ko/ja/zh/en
    PaddleOcr ocr(models);
    QString error;
    ocr.warmUp(ocrCode, true);

    QJsonArray rows;
    struct Tally { int reads = 0, exact = 0, marks = 0, marksKept = 0, dropped = 0; double unsure = 0; };
    QMap<QString, Tally> byCategory;
    std::printf("%-12s %-2s %-5s %-15s %-6s %-6s %-5s  %s\n", "category", "lg", "style", "font", "exact", "marks", "low", "read");
    for (const Sample& sample : samples()) {
        for (const QString& family : fontsFor(sample.language)) {
            for (const bool game : {false, true}) {
                const auto result = ocr.read(draw(sample.text, family, game), ocrCode, error);
                const QString read = result.text;
                double lowest = 1, lowestLetter = 1;
                for (const auto& line : result.lines) {
                    lowest = std::min(lowest, line.lowestCharacter);
                    lowestLetter = std::min(lowestLetter, line.lowestLetter);
                }
                const bool exact = compact(read) == compact(sample.text);
                int kept = 0;
                for (const QString& mark : sample.marks) kept += compact(read).contains(compact(mark));
                Tally& t = byCategory[QString::fromLatin1(sample.category)];
                ++t.reads; t.exact += exact; t.marks += int(sample.marks.size()); t.marksKept += kept;
                t.dropped += !result.dropped.isEmpty(); t.unsure += lowest < 0.9;
                const Split truthSplit = split(sample.text, targetFor(sample.language), ocrCode);
                QStringList unsure; // characters read with less than 0.9, and where they stand
                for (const auto& line : result.lines)
                    for (qsizetype k = 0; k < line.pieces.size(); ++k)
                        if (line.pieces[k].probability < 0.9)
                            unsure << QStringLiteral("%1@%2/%3=%4").arg(line.pieces[k].text).arg(k + 1).arg(line.pieces.size())
                                          .arg(line.pieces[k].probability, 0, 'f', 2);
                std::printf("%-12s %-2s %-5s %-15s %-6s %d/%-4d %.2f  %s%s%s\n", sample.category, sample.language, game ? "game" : "plain",
                            qPrintable(family), exact ? "yes" : "NO", kept, int(sample.marks.size()), lowest,
                            qPrintable(QString(read).replace('\n', '|')),
                            result.dropped.isEmpty() ? "" : qPrintable(QStringLiteral("  (dropped: %1)").arg(result.dropped.join('|'))),
                            unsure.isEmpty() ? "" : qPrintable(QStringLiteral("  unsure: %1").arg(unsure.join(' '))));
                if (!exact)
                    for (const auto& line : result.lines)
                        if (!line.others.isEmpty())
                            std::printf("%55s chosen %s, other models: %s\n", "", qPrintable(line.model.section('_', 0, 0)),
                                        qPrintable(line.others.join(QStringLiteral(" | "))));
                rows << QJsonObject{
                    {"category", sample.category}, {"language", sample.language}, {"font", family}, {"style", game ? "game" : "plain"},
                    {"truth", sample.text}, {"read", read}, {"exact", exact},
                    {"characterErrors", editDistance(compact(read), compact(sample.text))},
                    {"marks", QJsonArray::fromStringList(sample.marks)}, {"marksKept", kept},
                    {"lowestCharacter", lowest}, {"lowestLetter", lowestLetter}, {"dropped", QJsonArray::fromStringList(result.dropped)},
                    {"finishedTruth", WindowChecks::finished(sample.text)}, {"finishedRead", WindowChecks::finished(read)},
                    {"lineKey", WindowChecks::key(sample.text)},
                    {"target", targetFor(sample.language)},
                    {"sentToTranslation", QJsonArray::fromStringList(truthSplit.sent)},
                    {"keptLocally", QJsonArray::fromStringList(truthSplit.kept)}};
            }
        }
    }
    std::puts("\ncategory      reads exact  marks kept  dropped  unsure(<0.9)");
    QJsonObject summary;
    for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it) {
        const Tally& t = it.value();
        std::printf("%-12s %6d %5d %6d/%-4d %7d %8.0f\n", qPrintable(it.key()), t.reads, t.exact, t.marksKept, t.marks, t.dropped, t.unsure);
        summary[it.key()] = QJsonObject{{"reads", t.reads}, {"exact", t.exact}, {"marks", t.marks}, {"marksKept", t.marksKept},
                                        {"dropped", t.dropped}, {"unsure", t.unsure}};
    }

    // The app's rules on the true text, once per line.
    std::puts("\nline                          finished  sent to DeepL | kept");
    QJsonArray rules;
    for (const Sample& sample : samples()) {
        const Split truthSplit = split(sample.text, targetFor(sample.language), ocrCode);
        std::printf("%-30s %-8s  [%s] | [%s]\n", qPrintable(sample.text), WindowChecks::finished(sample.text) ? "yes" : "no",
                    qPrintable(truthSplit.sent.join(QStringLiteral("] ["))), qPrintable(truthSplit.kept.join(QStringLiteral("] ["))));
        rules << QJsonObject{{"line", sample.text}, {"finished", WindowChecks::finished(sample.text)},
                             {"sent", QJsonArray::fromStringList(truthSplit.sent)}, {"kept", QJsonArray::fromStringList(truthSplit.kept)},
                             {"source", truthSplit.source}, {"target", targetFor(sample.language)}};
    }

    QJsonArray translations;
    QJsonObject usage;
    if (deepl) {
        QString keyError;
        const QString key = Credentials::load(keyError);
        if (key.isEmpty()) { std::printf("no DeepL key: %s\n", qPrintable(keyError)); return 3; }
        const bool free = key.endsWith(QLatin1String(":fx"));
        TranslationService service;
        qint64 usedBefore = -1, usedAfter = -1, limit = -1;
        QObject::connect(&service, &TranslationService::usageChecked, [&](qint64 used, qint64 max) { (usedBefore < 0 ? usedBefore : usedAfter) = used; limit = max; });
        service.checkUsage(key, free); wait(service);
        qint64 sentCharacters = 0;
        std::puts("\nDeepL: line -> translation (ms)");
        for (const Sample& sample : samples()) {
            const Split s = split(sample.text, targetFor(sample.language), ocrCode);
            if (s.sent.isEmpty()) continue;
            QStringList got; QString detected, failure;
            auto ok = QObject::connect(&service, &TranslationService::translated, [&](const QStringList& r, const QString& d) { got = r; detected = d; });
            auto bad = QObject::connect(&service, &TranslationService::failed, [&](const QString& e) { failure = e; });
            QElapsedTimer clock; clock.start();
            service.translate(s.sent, key, free, targetFor(sample.language), s.source);
            wait(service);
            const qint64 ms = clock.elapsed();
            QObject::disconnect(ok); QObject::disconnect(bad);
            for (const QString& piece : s.sent) sentCharacters += piece.size();
            std::vector<std::string> pieces;
            for (const QString& g : got) pieces.push_back(g.toStdString());
            const QString joined = QString::fromStdString(subtitle::joinTranslated(
                subtitle::splitForTranslation(sample.text.toStdString(), subtitle::preservableScripts(
                    targetFor(sample.language).section('-', 0, 0).toLower().toStdString(), Languages::deeplSource(ocrCode).toLower().toStdString())), pieces));
            int kept = 0;
            for (const QString& mark : sample.marks) kept += joined.contains(mark);
            std::printf("%-26s -> %s (%lld ms)%s\n", qPrintable(sample.text), qPrintable(joined.isEmpty() ? failure : joined), ms,
                        sample.marks.isEmpty() ? "" : qPrintable(QStringLiteral("  marks %1/%2").arg(kept).arg(sample.marks.size())));
            translations << QJsonObject{{"line", sample.text}, {"sent", QJsonArray::fromStringList(s.sent)}, {"source", s.source},
                                        {"target", targetFor(sample.language)}, {"translation", joined}, {"error", failure},
                                        {"ms", ms}, {"marks", QJsonArray::fromStringList(sample.marks)}, {"marksKept", kept}};
        }
        QThread::msleep(1500); // the usage count trails a moment behind
        service.checkUsage(key, free); wait(service);
        std::printf("\nDeepL characters: sent %lld, usage %lld -> %lld of %lld\n", sentCharacters, usedBefore, usedAfter, limit);
        usage = QJsonObject{{"sentCharacters", sentCharacters}, {"usedBefore", usedBefore}, {"usedAfter", usedAfter}, {"limit", limit}};
    }

    QFile file(QString::fromLocal8Bit(argv[1]));
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(QJsonObject{{"ocrLanguages", ocrCode}, {"reads", rows}, {"summary", summary},
                                             {"rules", rules}, {"deepl", translations}, {"usage", usage}}).toJson());
    return 0;
}
