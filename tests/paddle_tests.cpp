#include "ocr_service.h"
#include "paddle_ocr.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
namespace {
QString fixture(const QString& name) { return QDir(QStringLiteral(OCR_FIXTURE_DIR)).filePath(name); }
QString models() { return qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); }
}
class PaddleChecks : public QObject {
    Q_OBJECT
private slots:
    // Thresholds from docs/plan/2026-10-25-ocr-false-text.ko.md; probabilities below are measured values.
    void filterLineDropsMisreadSymbolsOnly() {
        using Piece = PaddleRecognizer::Piece;
        auto pieces = [](std::initializer_list<std::pair<const char*, float>> list) {
            QList<Piece> out;
            for (const auto& [text, p] : list) out << Piece{QString::fromUtf8(text), p};
            return out;
        };
        auto text = [](const PaddleOcr::Filtered& f) {
            QString s; for (const auto& p : f.kept) s += p.text; return s;
        };
        // "○" read as "O" (0.78) and "「」" as "J]" (0.31): dropped.
        QVERIFY(PaddleOcr::filterLine(pieces({{"O", 0.78f}})).drop);
        QVERIFY(PaddleOcr::filterLine(pieces({{"J", 0.31f}, {"]", 0.9f}})).drop);
        // Real short lines with one sure letter stay, however unsure the rest (いいえ: 0.59, 0.73, 0.99).
        QVERIFY(!PaddleOcr::filterLine(pieces({{"い", 0.59f}, {"い", 0.73f}, {"え", 0.99f}})).drop);
        QVERIFY(!PaddleOcr::filterLine(pieces({{"응", 0.917f}})).drop);
        QVERIFY(!PaddleOcr::filterLine(pieces({{"え", 1.0f}, {"?", 0.2f}})).drop);
        // Symbols only: nothing to translate, left as read.
        const auto arrows = PaddleOcr::filterLine(pieces({{"↓", 0.5f}, {"↑", 0.4f}}));
        QVERIFY(!arrows.drop && !arrows.changed);
        // "♪ Hello ♪" read as "s Hello s" (0.25, 0.43): the unsure lone letters at the ends go.
        const auto hello = PaddleOcr::filterLine(pieces({{"s", 0.25f}, {" ", 0.9f}, {"H", 1.0f}, {"e", 1.0f}, {"l", 0.65f},
                                                         {"l", 1.0f}, {"o", 1.0f}, {" ", 0.9f}, {"s", 0.43f}}));
        QVERIFY(hello.changed); QCOMPARE(text(hello), QStringLiteral("Hello"));
        // A sure lone letter stays ("Я же" 0.77), and so does an unsure one in the middle ("salvar o progresso").
        QVERIFY(!PaddleOcr::filterLine(pieces({{"Я", 0.77f}, {" ", 1.0f}, {"ж", 1.0f}, {"е", 1.0f}})).changed);
        QVERIFY(!PaddleOcr::filterLine(pieces({{"a", 1.0f}, {" ", 1.0f}, {"o", 0.4f}, {" ", 1.0f}, {"b", 1.0f}, {"c", 1.0f}})).changed);
        // Devanagari words read with low probability even when right ("है" 0.32 at the end): not touched.
        QVERIFY(!PaddleOcr::filterLine(pieces({{"क", 1.0f}, {"ब", 1.0f}, {" ", 1.0f}, {"है", 0.32f}})).changed);
        // Arabic pieces are in visual order: only the short-line rule applies.
        QVERIFY(!PaddleOcr::filterLine(pieces({{"ب", 0.3f}, {" ", 1.0f}, {"ل", 1.0f}, {"ا", 1.0f}, {"م", 1.0f}}), true).changed);
    }
    void decodeDropsRepeatsAndBlanks() {
        const QStringList dictionary{QStringLiteral("blank"), QStringLiteral("a"), QStringLiteral("b"), QStringLiteral(" ")};
        // steps: a a blank a b b space  ->  "aab " (a blank separates the two a's)
        const float p[] = {0.0f, 0.9f, 0.1f, 0.0f,   0.0f, 0.8f, 0.2f, 0.0f,   0.7f, 0.1f, 0.1f, 0.1f,
                           0.1f, 0.6f, 0.2f, 0.1f,   0.1f, 0.1f, 0.8f, 0.0f,   0.0f, 0.1f, 0.9f, 0.0f,
                           0.1f, 0.0f, 0.1f, 0.8f};
        const auto line = PaddleRecognizer::decode(p, 7, 4, dictionary);
        QCOMPARE(line.text, QStringLiteral("aab "));
        QVERIFY(std::abs(line.confidence - (0.9 + 0.6 + 0.8 + 0.8) / 4) < 1e-6); // kept steps only
        const float blanks[] = {0.9f, 0.1f, 0.0f, 0.0f, 0.8f, 0.2f, 0.0f, 0.0f};
        QCOMPARE(PaddleRecognizer::decode(blanks, 2, 4, dictionary).confidence, 0.0);
    }
    void inputTensorMatchesRapidOcr() {
        QImage white(100, 20, QImage::Format_RGB32); white.fill(Qt::white);
        int width = 0;
        const auto data = PaddleRecognizer::inputTensor(white, width);
        // 100x20 -> short side to 30, rounded to 32: 160x32 (ratio 5) -> height 48, width max(320, 240) = 320
        QCOMPARE(width, 320);
        QCOMPARE(data.size(), size_t(3 * 48 * 320));
        QCOMPARE(data[0], 1.0f);                  // white scaled to +1
        QCOMPARE(data[size_t(10) * 320 + 300], 0.0f); // right side is zero padding
        QImage blue(64, 48, QImage::Format_RGB32); blue.fill(QColor(0, 0, 255));
        const auto bgr = PaddleRecognizer::inputTensor(blue, width);
        QCOMPARE(bgr[0], 1.0f);                           // channel 0 is blue (BGR order)
        QCOMPARE(bgr[size_t(2) * 48 * width], -1.0f);     // channel 2 is red
    }
    void detectorInputIsEnlargedWithinLimits() {
        QImage white(100, 20, QImage::Format_RGB32); white.fill(Qt::white);
        int width = 0, height = 0;
        const auto data = PaddleDetector::inputTensor(white, width, height);
        QCOMPARE(width, 320); QCOMPARE(height, 64); // shorter side raised to 64 (x3.2)
        QCOMPARE(data.size(), size_t(3 * 320 * 64));
        QCOMPARE(data[0], 1.0f);
        QImage dialog(1000, 300, QImage::Format_RGB32); dialog.fill(QColor(0, 0, 255));
        const auto bgr = PaddleDetector::inputTensor(dialog, width, height);
        QCOMPARE(width, 1504); QCOMPARE(height, 448); // x1.5, rounded to multiples of 32
        QCOMPARE(bgr[0], 1.0f);                                   // channel 0 is blue (BGR order)
        QCOMPARE(bgr[size_t(2) * width * height], -1.0f);          // channel 2 is red
        QImage screen(1920, 1080, QImage::Format_RGB32); screen.fill(Qt::black);
        PaddleDetector::inputTensor(screen, width, height);
        QVERIFY(qint64(width) * height <= 2'100'000); // a whole screen stays near native scale
    }
    void detectorBoxesFromProbabilityMap() {
        std::vector<float> map(100 * 40, 0.0f);
        auto fill = [&](int x0, int y0, int x1, int y1, float value) {
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) map[size_t(y) * 100 + x] = value;
        };
        fill(20, 15, 79, 24, 0.9f); // a text line
        fill(85, 30, 95, 37, 0.35f); // above the pixel threshold, but a weak region on average
        fill(2, 2, 3, 3, 0.9f);      // too small to be a glyph
        // Core 20..80 x 15..25 after dilation, grown by 60*10*1.6/(2*70) = 6.86 px on each side.
        QCOMPARE(PaddleDetector::boxes(map.data(), 100, 40, QSize(100, 40)), QList<QRect>{QRect(QPoint(13, 8), QPoint(87, 32))});
        QCOMPARE(PaddleDetector::boxes(map.data(), 100, 40, QSize(200, 80)), QList<QRect>{QRect(QPoint(26, 16), QPoint(174, 64))});
    }
    void detectorJoinsWordsOfOneLine() {
        const QList<QRect> lines = PaddleDetector::joinLines({QRect(10, 50, 80, 20), QRect(70, 12, 40, 18), QRect(10, 10, 50, 20)});
        QCOMPARE(lines, (QList<QRect>{QRect(QPoint(10, 10), QPoint(109, 29)), QRect(10, 50, 80, 20)}));
        // Stacked lines that touch are not joined.
        QCOMPARE(PaddleDetector::joinLines({QRect(0, 0, 100, 20), QRect(0, 15, 100, 20)}).size(), 2);
    }
    void textLinesFindsEachLine() {
        QImage image(400, 140, QImage::Format_RGB32); image.fill(QColor(235, 225, 230));
        QPainter p(&image); QFont f("Segoe UI"); f.setPixelSize(22); p.setFont(f); p.setPen(Qt::black);
        p.drawText(30, 40, QStringLiteral("First line of text")); p.drawText(30, 105, QStringLiteral("Second line here")); p.end();
        const auto lines = textLines(image);
        QCOMPARE(lines.size(), 2);
        for (const QImage& line : lines) { QVERIFY(line.height() < 60); QVERIFY(line.width() < 400); }
        QImage blank(200, 80, QImage::Format_RGB32); blank.fill(Qt::white);
        QCOMPARE(textLines(blank).size(), 1); // nothing found: the whole region
        QCOMPARE(textLines(blank).first().size(), blank.size());
    }
    void languageRouting() {
        QVERIFY(PaddleOcr::needsKorean("jpn+eng+kor") && PaddleOcr::needsCjk("jpn+eng+kor"));
        QVERIFY(PaddleOcr::needsKorean("kor") && !PaddleOcr::needsCjk("kor"));
        QVERIFY(!PaddleOcr::needsKorean("jpn") && PaddleOcr::needsCjk("jpn"));
        QVERIFY(!PaddleOcr::needsKorean("eng") && PaddleOcr::needsCjk("eng"));
        QVERIFY(!PaddleOcr::available(QStringLiteral("Z:/missing"), "jpn+eng+kor"));
        QVERIFY(!PaddleOcr::available(QString(), "jpn"));
    }
    void arabicHasLogicalOrder() {
        QCOMPARE(arabicLogicalOrder(QStringLiteral("؟ةبعللا ظفح ديرت له")), QStringLiteral("هل تريد حفظ اللعبة؟"));
        QCOMPARE(arabicLogicalOrder(QStringLiteral("100 HP")), QStringLiteral("100 HP"));
        QCOMPARE(arabicLogicalOrder(QStringLiteral("100 HP ةوقلا")), QStringLiteral("القوة 100 HP"));
    }
    void multilingualImages_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");
        QFile manifest(fixture("multilang/manifest.json"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        for (const auto value : QJsonDocument::fromJson(manifest.readAll()).array()) {
            const auto row = value.toObject();
            if (row["ocr"].toString() == "vie") continue; // covered by the real Tesseract service test
            QTest::newRow(qPrintable(row["lang"].toString())) << "multilang/" + row["file"].toString()
                << row["ocr"].toString() << row["truth"].toString();
        }
    }
    void multilingualImages() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured.");
        QFETCH(QString, file); QFETCH(QString, language); QFETCH(QString, expected);
        PaddleOcr ocr(models()); QString error;
        const auto result = ocr.read(QImage(fixture(file)), language, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.modelRuns, 1);
        QVERIFY(result.detected);
        // Every letter of the sentence, in logical Unicode order. Whitespace and punctuation are
        // measured in the benchmark (the detector can leave out a trailing full stop).
        auto compact = [](QString s) {
            return s.normalized(QString::NormalizationForm_C).remove(QRegularExpression("[\\s\\p{P}]"));
        };
        QCOMPARE(compact(result.text), compact(expected));
    }
    void vietnameseKeepsToneMarks() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Tesseract not configured.");
        OcrService service; service.setPaddleModelDir(models());
        QSignalSpy recognized(&service, &OcrService::recognized);
        service.recognize(QImage(fixture("multilang/vi-0-24-dark-on-light-tight.png")),
            qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"), qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"), 0, "vie");
        QTRY_COMPARE_WITH_TIMEOUT(recognized.count(), 1, 20000);
        QCOMPARE(recognized.first().first().toString(), QStringLiteral("Bạn có muốn lưu tiến trình không?"));
        QCOMPARE(service.engineUsed(), QStringLiteral("Tesseract"));
    }
    void multiMissingModelsHasActionableError() {
        OcrService service;
        QSignalSpy failed(&service, &OcrService::failed);
        // Missing-engine diagnostics need a nonblank input; flat images now finish without OCR.
        const QImage image(fixture("outlined-en-18px.png"));
        QVERIFY(!image.isNull());
        service.recognize(image, "C:/unused/tesseract.exe", {}, 0, "multi");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("PaddleOCR")));
        QVERIFY(!service.isBusy());
    }
    void gameTextThroughPaddle_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("expected");
        QTest::newRow("outlined english 18px") << QStringLiteral("outlined-en-18px.png") << QStringLiteral("continue");
        QTest::newRow("outlined korean 18px") << QStringLiteral("outlined-ko-18px.png") << QStringLiteral("날씨");
        QTest::newRow("outlined japanese 22px") << QStringLiteral("outlined-ja-22px.png") << QStringLiteral("天気");
        QTest::newRow("shadow english 14px") << QStringLiteral("shadow-en-14px.png") << QStringLiteral("Press any key"); // the model reads "conunue" here, same as the Python reference
        QTest::newRow("shadow korean 14px") << QStringLiteral("shadow-ko-14px.png") << QStringLiteral("날씨");
        QTest::newRow("shadow japanese 18px") << QStringLiteral("shadow-ja-18px.png") << QStringLiteral("天気");
        QTest::newRow("loose outlined english 12px") << QStringLiteral("loose-outlined-en-12px.png") << QStringLiteral("continue");
        QTest::newRow("loose outlined korean 14px") << QStringLiteral("loose-outlined-ko-14px.png") << QStringLiteral("날씨");
        QTest::newRow("loose shadow japanese 14px") << QStringLiteral("loose-shadow-ja-14px.png") << QStringLiteral("天気");
    }
    void gameTextThroughPaddle() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        QFETCH(QString, file); QFETCH(QString, expected);
        const QImage image(fixture(file));
        QVERIFY2(!image.isNull(), qPrintable(file));
        PaddleOcr ocr(models());
        QString error;
        const auto result = ocr.read(image, "jpn+eng+kor", error);
        QVERIFY2(error.isEmpty(), error.toUtf8().constData());
        QVERIFY2(result.text.contains(expected), result.text.toUtf8().constData());
        QCOMPARE(result.modelRuns, 2); // one line, both models compared
        QVERIFY(result.confidence > 0.5);
    }
    void singleLanguageUsesOneModel() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        PaddleOcr ocr(models());
        QString error;
        const auto korean = ocr.read(QImage(fixture("shadow-ko-14px.png")), "kor", error);
        QVERIFY(korean.text.contains(QStringLiteral("날씨"))); QCOMPARE(korean.modelRuns, 1);
        const auto japanese = ocr.read(QImage(fixture("shadow-ja-18px.png")), "jpn", error);
        QVERIFY(japanese.text.contains(QStringLiteral("天気"))); QCOMPARE(japanese.modelRuns, 1);
    }
    void autoDetectPrefersTheFullerReading() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        PaddleOcr ocr(models());
        QString error;
        // The Korean model read this Chinese line as a lone, confident "?" and used to win.
        const auto chinese = ocr.read(QImage(fixture("multilang/zh-Hans-0-24-dark-on-light-tight.png")), "jpn+eng+kor", error);
        QVERIFY2(chinese.text.contains(QStringLiteral("天气")), chinese.text.toUtf8().constData());
    }
    void multiLanguageAutoDetectIsStaged() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        PaddleOcr ocr(models());
        QString error;
        const auto korean = ocr.read(QImage(fixture("multilang/ko-0-24-dark-on-light-tight.png")), "multi", error);
        QVERIFY(korean.text.contains(QStringLiteral("날씨")));
        QCOMPARE(korean.modelRuns, 2); // Korean and Chinese/Japanese/English models were enough
        const auto french = ocr.read(QImage(fixture("multilang/fr-0-24-dark-on-light-tight.png")), "multi", error);
        QVERIFY2(french.text.contains(QStringLiteral("sauvegarder")), french.text.toUtf8().constData());
        QCOMPARE(french.modelRuns, 4); // Latin-only reading: the Latin and Cyrillic models are asked too
        const auto russian = ocr.read(QImage(fixture("multilang/ru-0-24-dark-on-light-tight.png")), "multi", error);
        QVERIFY2(russian.text.contains(QStringLiteral("Сохранить")), russian.text.toUtf8().constData());
        QVERIFY(russian.modelRuns >= 4); // look-alike Latin or doubtful reading: the Cyrillic model is asked
    }
    void decoratedDialogBoxIsReadLineByLine_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QStringList>("expected");
        QFile manifest(fixture("scene/manifest.json"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        for (const auto value : QJsonDocument::fromJson(manifest.readAll()).array()) {
            const auto row = value.toObject();
            // Dialog lines after the name tag; the name tag and the small UI buttons are measured
            // in the benchmark (a tag's border can turn into an extra jamo, e.g. 채주아 -> 채주악).
            QStringList lines = row["truth"].toString().split('\n');
            if (row["style"].toString() == "notebook") lines.removeLast();
            lines.removeFirst();
            QTest::newRow(qPrintable(row["file"].toString())) << "scene/" + row["file"].toString()
                << QStringLiteral("jpn+eng+kor") << lines;
        }
    }
    void decoratedDialogBoxIsReadLineByLine() {
        // The whole dialog box is selected: name tag, ruled lines, frame and scenery around the text.
        // The edge-band line finder read nothing or noise here; the detection model finds each line.
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        QFETCH(QString, file); QFETCH(QString, language); QFETCH(QStringList, expected);
        PaddleOcr ocr(models()); QString error;
        const auto result = ocr.read(QImage(fixture(file)), language, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(result.detected);
        auto compact = [](QString s) {
            return s.normalized(QString::NormalizationForm_KC).remove(QRegularExpression("[\\s\\p{P}]"));
        };
        for (const QString& line : expected)
            QVERIFY2(compact(result.text).contains(compact(line)), qPrintable(result.text));
        QVERIFY2(result.text.split('\n').size() > expected.size(), qPrintable(result.text)); // the name tag's line too
    }
    void withoutDetectionModelTheLineFinderReads() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString korean = QLatin1String(PaddleOcr::KoreanModel);
        QVERIFY(QFile::copy(QDir(models()).filePath(korean), dir.filePath(korean)));
        PaddleOcr ocr(dir.path()); QString error;
        const auto result = ocr.read(QImage(fixture("shadow-ko-14px.png")), "kor", error);
        QVERIFY(!result.detected); // older installs without the detection model keep working
        QVERIFY2(result.text.contains(QStringLiteral("날씨")), qPrintable(result.text));
    }
    void warmReadIsFast() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        PaddleOcr ocr(models());
        QString error;
        const QImage image(fixture("outlined-ko-18px.png"));
        ocr.read(image, "jpn+eng+kor", error); // loads both models once, as the app does
        QList<qint64> times;
        for (int i = 0; i < 7; ++i) {
            QElapsedTimer timer; timer.start();
            ocr.read(image, "jpn+eng+kor", error);
            times << timer.elapsed();
        }
        std::sort(times.begin(), times.end());
        qInfo("warm read, auto-detect (2 models): median %lld ms", times[times.size() / 2]);
        QVERIFY(times[times.size() / 2] < 1000);
    }
    void serviceUsesPaddleThenFallsBack() {
        if (models().isEmpty()) QSKIP("PaddleOCR models not configured (SUBTITLE_TEST_PADDLE).");
        OcrService service;
        service.setPaddleModelDir(models());
        QSignalSpy recognized(&service, &OcrService::recognized);
        service.recognize(QImage(fixture("outlined-ko-18px.png")), qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"),
                          qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"), 0, "jpn+eng+kor");
        QTRY_COMPARE_WITH_TIMEOUT(recognized.count(), 1, 20000);
        QCOMPARE(service.engineUsed(), QStringLiteral("PaddleOCR"));
        QVERIFY(recognized.first().first().toString().contains(QStringLiteral("날씨")));
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) return;
        // Missing models: Tesseract reads instead.
        service.setPaddleModelDir(QStringLiteral("Z:/missing-paddle"));
        QImage plain(1000, 120, QImage::Format_RGB32); plain.fill(Qt::white);
        QPainter p(&plain); QFont f("Segoe UI"); f.setPixelSize(48); p.setFont(f); p.setPen(Qt::black);
        p.drawText(plain.rect(), Qt::AlignCenter, QStringLiteral("Press any key to continue.")); p.end();
        service.recognize(plain, qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"), qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"), 0, "jpn+eng+kor");
        QTRY_COMPARE_WITH_TIMEOUT(recognized.count(), 2, 25000);
        QCOMPARE(service.engineUsed(), QStringLiteral("Tesseract"));
    }
};
QTEST_MAIN(PaddleChecks)
#include "paddle_tests.moc"
