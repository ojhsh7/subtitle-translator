#include "ocr_service.h"
#include "capture_service.h"
#include <QtTest>
#include <QSignalSpy>
#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QScreen>
#include <QApplication>
#include <QLabel>
namespace {
QImage rendered(const QString& text, const QString& family) {
    QImage fixture(1000,120,QImage::Format_RGB32); fixture.fill(Qt::white);
    QPainter painter(&fixture);
    QFont font(family); font.setPixelSize(48); painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(fixture.rect(), Qt::AlignCenter, text);
    painter.end();
    return fixture;
}
QString readWith(OcrService& service, const QImage& image) {
    QSignalSpy recognized(&service, &OcrService::recognized);
    QSignalSpy failed(&service, &OcrService::failed);
    service.recognize(image, qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"), qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"), 0, "jpn+eng+kor");
    if (!QTest::qWaitFor([&] { return recognized.count() + failed.count() > 0; }, 25000)) return QStringLiteral("<timeout>");
    return recognized.isEmpty() ? QStringLiteral("<failed>") : recognized.first().first().toString();
}
}
class LocalChecks : public QObject {
    Q_OBJECT
private slots:
    void combinedOutputKeepsSpacingAndConfidence() {
        // Shape of one Tesseract run with both TSV and plain text written to stdout.
        const QString output = QStringLiteral(
            "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\n"
            "4\t1\t1\t1\t1\t0\t0\t0\t10\t10\t-1\t\r\n"
            "5\t1\t1\t1\t1\t1\t0\t0\t10\t10\t90\t오\r\n"
            "5\t1\t1\t1\t1\t2\t0\t0\t10\t10\t80\t늘은\r\n"
            "5\t1\t1\t1\t1\t3\t0\t0\t10\t10\t70\t날씨\r\n"
            "오늘은 날씨\r\n"
            "Press any key\r\n");
        const auto reading = OcrService::parseOutput(output);
        QCOMPARE(reading.text, QStringLiteral("오늘은 날씨\nPress any key")); // plain-text spacing, not TSV word splits
        QVERIFY(qAbs(reading.confidence - (90 * 1 + 80 * 2 + 70 * 2) / 5.0) < 0.01); // per character
        QVERIFY(OcrService::parseOutput(QString()).text.isEmpty());
        QCOMPARE(OcrService::parseOutput(QString()).confidence, 0.0);
    }
    void lightTextMaskIsolatesWhiteText() {
        QImage image(40, 20, QImage::Format_RGB32); image.fill(QColor(90, 160, 220));
        for (int x = 10; x < 30; ++x) for (int y = 8; y < 12; ++y) image.setPixel(x, y, qRgb(250, 250, 250));
        double coverage = 0;
        const QImage mask = OcrService::lightTextMask(image, &coverage);
        QCOMPARE(mask.size(), QSize(120, 60));
        QCOMPARE(qGray(mask.pixel(60, 30)), 0);  // white stroke becomes black ink
        QCOMPARE(qGray(mask.pixel(5, 5)), 255);  // coloured background becomes white
        QVERIFY(coverage > 0.05 && coverage < 0.15);
        auto ink = [](const QImage& m) { int n = 0; for (int y = 0; y < m.height(); ++y) for (int x = 0; x < m.width(); ++x) n += qGray(m.pixel(x, y)) < 128; return n; };
        QVERIFY(ink(OcrService::growInk(mask)) > ink(mask));
        QImage fringed(40, 20, QImage::Format_RGB32); fringed.fill(QColor(30, 30, 40));
        for (int x = 10; x < 30; ++x) for (int y = 8; y < 12; ++y) fringed.setPixel(x, y, qRgb(255, 200, 150)); // colour fringe
        double colourless = 0, anyColour = 0;
        OcrService::lightTextMask(fringed, &colourless);
        OcrService::lightTextMask(fringed, &anyColour, true);
        QCOMPARE(colourless, 0.0);   // too colourful for the white-text mask
        QVERIFY(anyColour > 0.05);   // but bright enough for the brightness-only mask
    }
    void whiteGameTextOnBusyBackground_data() {
        // Images from scripts/make-ocr-fixtures.py: white text with a 1 px outline or shadow on a noisy gradient.
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("expected");
        QTest::newRow("outlined english 18px") << QStringLiteral("outlined-en-18px.png") << QStringLiteral("continue");
        QTest::newRow("outlined korean 18px") << QStringLiteral("outlined-ko-18px.png") << QStringLiteral("날씨");
        QTest::newRow("outlined japanese 22px") << QStringLiteral("outlined-ja-22px.png") << QStringLiteral("天気");
        QTest::newRow("shadow english 14px") << QStringLiteral("shadow-en-14px.png") << QStringLiteral("continue");
        QTest::newRow("shadow korean 14px") << QStringLiteral("shadow-ko-14px.png") << QStringLiteral("날씨");
        QTest::newRow("shadow japanese 18px") << QStringLiteral("shadow-ja-18px.png") << QStringLiteral("天気");
    }
    void whiteGameTextOnBusyBackground() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("External OCR engine not configured: actual OCR test skipped.");
        QFETCH(QString, file); QFETCH(QString, expected);
        const QImage fixture(QDir(QStringLiteral(OCR_FIXTURE_DIR)).filePath(file));
        QVERIFY2(!fixture.isNull(), qPrintable(file));
        OcrService service;
        const QString result = readWith(service, fixture);
        QVERIFY2(result.contains(expected), result.toUtf8().constData());
    }
    void cleanTextFixesStandaloneI() {
        QCOMPARE(OcrService::cleanText("| told you, didn't |?"), QString("I told you, didn't I?"));
        QCOMPARE(OcrService::cleanText("a|b"), QString("a|b"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("その扉")), QStringLiteral("その扉"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("今日 はいい天気です 。")), QStringLiteral("今日はいい天気です。"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("言ったでしょう ?")), QStringLiteral("言ったでしょう?"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("오늘은 날씨가 좋습니다.")), QStringLiteral("오늘은 날씨가 좋습니다."));
        QCOMPARE(OcrService::cleanText(QStringLiteral("HP 回復")), QStringLiteral("HP 回復")); // Latin next to Han keeps its space
    }
    void automaticLayoutReadsStripsAsOneLine() {
        QCOMPARE(OcrService::resolvePsm(0, QSize(400,42)), 7);
        QCOMPARE(OcrService::resolvePsm(0, QSize(800,120)), 6);
        QCOMPARE(OcrService::resolvePsm(6, QSize(400,42)), 6); // An explicit choice wins.
        QCOMPARE(OcrService::resolvePsm(7, QSize(800,120)), 7);
    }
    void autoDetectReadsEachLanguage_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("expected");
        QTest::newRow("english") << QStringLiteral("Press any key to continue.") << QStringLiteral("Segoe UI") << QStringLiteral("continue");
        QTest::newRow("korean") << QStringLiteral("오늘은 날씨가 좋습니다.") << QStringLiteral("Malgun Gothic") << QStringLiteral("날씨");
        QTest::newRow("japanese") << QStringLiteral("今日はいい天気です。") << QStringLiteral("Yu Gothic") << QStringLiteral("天気");
    }
    void autoDetectReadsEachLanguage() {
        const QString exe = qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        const QString data = qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        if (exe.isEmpty()) QSKIP("External OCR engine not configured: actual OCR test skipped.");
        QFETCH(QString, text); QFETCH(QString, family); QFETCH(QString, expected);
        OcrService service;
        QSignalSpy recognized(&service, &OcrService::recognized);
        QSignalSpy failed(&service, &OcrService::failed);
        service.recognize(rendered(text, family), exe, data, 6, "jpn+eng+kor");
        QTRY_VERIFY_WITH_TIMEOUT(recognized.count() + failed.count() > 0, 25000);
        QVERIFY2(failed.isEmpty(), "Actual OCR failed.");
        const auto result = recognized.first().first().toString();
        QVERIFY2(result.contains(expected), qPrintable(result));
    }
    void unsupportedLanguageIsRejected() {
        OcrService service;
        QSignalSpy failed(&service, &OcrService::failed);
        service.recognize(QImage(300,100,QImage::Format_RGB32), "C:/unused/tesseract.exe", {}, 6, "jpn -c x=1");
        QCOMPARE(failed.count(), 1);
        QVERIFY(!service.isBusy());
    }
    void rejectInvalidCapture() {
        QString error;
        QVERIFY(Capture::region(nullptr, QRect(0,0,100,100), error).isNull());
        QVERIFY(!error.isEmpty());
    }
    void nonexistentOcrExecutable() {
        OcrService service;
        QSignalSpy failed(&service, &OcrService::failed);
        QImage image(300,100,QImage::Format_RGB32); image.fill(Qt::white);
        service.recognize(image, "Z:/subtitle-missing/tesseract.exe", {}, 6);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(!service.isBusy());
    }
    void japaneseOcrActualEngine() {
        const QString exe = qEnvironmentVariable("SUBTITLE_TEST_TESSERACT");
        const QString data = qEnvironmentVariable("SUBTITLE_TEST_TESSDATA");
        if (exe.isEmpty()) QSKIP("External OCR engine not configured: actual OCR test skipped.");
        QImage fixture(1000,180,QImage::Format_RGB32); fixture.fill(Qt::white);
        QPainter painter(&fixture);
        QFont font("Yu Gothic"); font.setPixelSize(52); painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(fixture.rect(), Qt::AlignCenter, QStringLiteral("今日はいい天気です。"));
        painter.end();
        OcrService service;
        QSignalSpy recognized(&service, &OcrService::recognized);
        QSignalSpy failed(&service, &OcrService::failed);
        service.recognize(fixture, exe, data, 6);
        QTRY_VERIFY_WITH_TIMEOUT(recognized.count() + failed.count() > 0, 25000);
        QVERIFY2(failed.isEmpty(), "Actual OCR failed.");
        const auto text = recognized.first().first().toString();
        QVERIFY2(text.contains(QStringLiteral("天気")), qPrintable(text));
    }
};
QTEST_MAIN(LocalChecks)
#include "integration_checks.moc"
