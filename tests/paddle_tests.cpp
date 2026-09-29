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
#include <QtTest>
#include <cmath>
namespace {
QString fixture(const QString& name) { return QDir(QStringLiteral(OCR_FIXTURE_DIR)).filePath(name); }
QString models() { return qEnvironmentVariable("SUBTITLE_TEST_PADDLE"); }
}
class PaddleChecks : public QObject {
    Q_OBJECT
private slots:
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
        // Whole sentence, in logical Unicode order. Whitespace differences are checked in the benchmark.
        auto compact = [](QString s) { return s.normalized(QString::NormalizationForm_C).remove(QRegularExpression("\\s")); };
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
        QImage image(200,50,QImage::Format_RGB32); image.fill(Qt::white);
        service.recognize(image, "C:/unused/tesseract.exe", {}, 0, "multi");
        QCOMPARE(failed.count(), 1);
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
