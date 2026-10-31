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

    void uniformCheckPreservesFaintPixelsAndTinyText() {
        QImage image(400,80,QImage::Format_RGB32); image.fill(qRgb(128,128,128));
        QVERIFY(OcrService::isUniformImage(image));
        image.setPixel(399,79,qRgb(128,128,129)); // One faint pixel, even at the last position, is not blank.
        QVERIFY(!OcrService::isUniformImage(image));
        QVERIFY(!OcrService::isUniformImage(rendered(QStringLiteral("はい"),"Yu Gothic")));
        QVERIFY(!OcrService::isUniformImage(QImage()));
        QImage gray(17,9,QImage::Format_Grayscale8); gray.fill(40);
        QVERIFY(OcrService::isUniformImage(gray)); // Padding bytes must not affect equality.
    }
    void cancelledBlankCheckNeverEmitsStaleResult() {
        OcrService service;
        QSignalSpy recognized(&service,&OcrService::recognized);
        QSignalSpy failed(&service,&OcrService::failed);
        QImage image(300,100,QImage::Format_RGB32); image.fill(Qt::white);
        service.recognize(image,{},{});
        service.cancel();
        QTest::qWait(200);
        QCOMPARE(recognized.size(),0);
        QCOMPARE(failed.size(),0);
        QVERIFY(!service.isBusy());
    }

    void blankImageNeverProducesText_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QColor>("colour");
        QTest::addColumn<bool>("paddle");
        for (bool paddle : {false, true}) {
            const QByteArray prefix = paddle ? "paddle-" : "tesseract-";
            QTest::newRow((prefix+"white-small").constData()) << QSize(200,60) << QColor(Qt::white) << paddle;
            QTest::newRow((prefix+"black-small").constData()) << QSize(200,60) << QColor(Qt::black) << paddle;
            QTest::newRow((prefix+"gray").constData()) << QSize(600,120) << QColor(32,32,32) << paddle;
            QTest::newRow((prefix+"white-large").constData()) << QSize(1200,300) << QColor(Qt::white) << paddle;
        }
    }
    void blankImageNeverProducesText() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual Tesseract not configured");
        QFETCH(QSize,size); QFETCH(QColor,colour); QFETCH(bool,paddle);
        if (paddle && qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("Actual Paddle models not configured");
        OcrService service;
        if (paddle) service.setPaddleModelDir(qEnvironmentVariable("SUBTITLE_TEST_PADDLE"));
        QImage image(size,QImage::Format_RGB32); image.fill(colour);
        for (int repeat=0; repeat<3; ++repeat) {
            QSignalSpy recognized(&service,&OcrService::recognized);
            QSignalSpy failed(&service,&OcrService::failed);
            service.recognize(image,qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"),
                              qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"));
            QTRY_VERIFY_WITH_TIMEOUT(!recognized.isEmpty() || !failed.isEmpty(),25000);
            QVERIFY2(failed.isEmpty(),"A blank image is empty input, not an engine failure.");
            QCOMPARE(recognized.size(),1);
            QVERIFY2(recognized.first().first().toString().isEmpty(),
                     qPrintable(recognized.first().first().toString()));
            QCOMPARE(service.attemptsUsed(),0); // Do not ask either engine to invent text from a flat image.
            QVERIFY(!service.isBusy());
        }
    }
    // P-01/P-02: images from tests/probes/false-text-probe.ps1 (docs/plan/2026-10-25-ocr-false-text.ko.md).
    void falseTextIsNotRead_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("paddle");
        QTest::addColumn<QString>("expected"); // letters and digits only; empty = no text
        // Empty dialogue boxes: the detector finds no line, so Tesseract must not invent one.
        QTest::newRow("paddle-frame") << "N-dark-frame-820x140" << "jpn+eng+kor" << true << "";
        QTest::newRow("paddle-border") << "N-white-border-200x60" << "jpn+eng+kor" << true << "";
        QTest::newRow("paddle-noise") << "N-dark-noise4-820x140" << "jpn+eng+kor" << true << "";
        QTest::newRow("paddle-noise-small") << "N-white-noise2-200x60" << "jpn+eng+kor" << true << "";
        // Symbols: "♥ ☆ ◎ ✕ ○ △" was read as "O"; "♪ Hello ♪" as "s Hello s".
        QTest::newRow("paddle-symbols") << "s1-misc-36-dark-on-light" << "jpn+eng+kor" << true << "";
        QTest::newRow("paddle-hello") << "s3-en-note-24-dark-on-light" << "jpn+eng+kor" << true << "Hello";
        // Real short lines with the lowest measured confidence stay exactly as they are.
        QTest::newRow("paddle-iie") << "k-iie-16-dark-on-light" << "jpn+eng+kor" << true << QStringLiteral("いいえ");
        QTest::newRow("paddle-ne") << "k-ne-16-dark-on-light" << "jpn+eng+kor" << true << QStringLiteral("네");
        // Tesseract only: garbage at confidence 13 goes, a real "응" read at 36 stays.
        QTest::newRow("tesseract-noise") << "N-dark-noise4-820x140" << "jpn+eng+kor" << false << "";
        QTest::newRow("tesseract-eung") << "k-eung-16-dark-on-light" << "kor" << false << QStringLiteral("응");
    }
    void falseTextIsNotRead() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual Tesseract not configured");
        QFETCH(QString, file); QFETCH(QString, language); QFETCH(bool, paddle); QFETCH(QString, expected);
        if (paddle && qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("Actual Paddle models not configured");
        const QImage image(QDir(QStringLiteral(OCR_FIXTURE_DIR)).filePath(QStringLiteral("false-text/%1.png").arg(file)));
        QVERIFY(!image.isNull());
        OcrService service;
        if (paddle) service.setPaddleModelDir(qEnvironmentVariable("SUBTITLE_TEST_PADDLE"));
        QSignalSpy recognized(&service, &OcrService::recognized);
        QSignalSpy failed(&service, &OcrService::failed);
        service.recognize(image, qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"), qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"), 0, language);
        QTRY_VERIFY_WITH_TIMEOUT(!recognized.isEmpty() || !failed.isEmpty(), 25000);
        QVERIFY2(failed.isEmpty(), "No text is an empty reading, not an engine failure.");
        const QString read = recognized.first().first().toString();
        QString letters;
        for (const QChar c : read) if (c.isLetterOrNumber()) letters += c;
        QVERIFY2(letters == expected, qPrintable(QStringLiteral("read [%1], engine %2").arg(read, service.engineUsed())));
    }
    void shortRealTextIsPreserved_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("paddle");
        for (bool paddle : {false,true}) {
            const QByteArray prefix = paddle ? "paddle-" : "tesseract-";
            QTest::newRow((prefix+"yes-ja").constData()) << QStringLiteral("はい") << QString("jpn") << paddle;
            QTest::newRow((prefix+"no-ja").constData()) << QStringLiteral("いいえ") << QString("jpn") << paddle;
            QTest::newRow((prefix+"ok-en").constData()) << QStringLiteral("OK") << QString("eng") << paddle;
        }
    }
    void shortRealTextIsPreserved() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual Tesseract not configured");
        QFETCH(QString,text); QFETCH(QString,language); QFETCH(bool,paddle);
        if (paddle && qEnvironmentVariableIsEmpty("SUBTITLE_TEST_PADDLE")) QSKIP("Actual Paddle models not configured");
        OcrService service;
        if (paddle) service.setPaddleModelDir(qEnvironmentVariable("SUBTITLE_TEST_PADDLE"));
        QSignalSpy recognized(&service,&OcrService::recognized);
        QSignalSpy failed(&service,&OcrService::failed);
        service.recognize(rendered(text,language=="eng" ? "Segoe UI" : "Yu Gothic"),
                          qEnvironmentVariable("SUBTITLE_TEST_TESSERACT"),
                          qEnvironmentVariable("SUBTITLE_TEST_TESSDATA"),7,language);
        QTRY_VERIFY_WITH_TIMEOUT(!recognized.isEmpty() || !failed.isEmpty(),25000);
        QVERIFY(failed.isEmpty());
        QCOMPARE(recognized.first().first().toString(),text);
    }
    void cursorRegionUsesLogicalCoordinatesAndStaysOnOneScreen() {
        const QRect primary(0,0,1920,1080);
        const QSize box(640,160);
        QCOMPARE(Capture::aroundPoint(primary,QPoint(960,540),box),QRect(640,460,640,160));
        QCOMPARE(Capture::aroundPoint(primary,QPoint(0,0),box),QRect(0,0,640,160));
        QCOMPARE(Capture::aroundPoint(primary,QPoint(1919,1079),box),QRect(1280,920,640,160));
        const QRect left(-1920,-200,1920,1080);
        QCOMPARE(Capture::aroundPoint(left,QPoint(-960,340),box),QRect(640,460,640,160));
        QCOMPARE(Capture::aroundPoint(left,QPoint(-1,879),box),QRect(1280,920,640,160));
        QCOMPARE(Capture::aroundPoint(QRect(200,300,320,100),QPoint(201,301),box),QRect(0,0,320,100));
        QVERIFY(Capture::aroundPoint(primary,QPoint(-1,100),box).isEmpty());
        QVERIFY(Capture::aroundPoint(primary,QPoint(1920,100),box).isEmpty());
        QVERIFY(Capture::aroundPoint(primary,QPoint(100,100),QSize(-1,160)).isEmpty());
        QVERIFY(Capture::aroundPoint(QRect(0,0,4,4),QPoint(1,1),box).isEmpty());
    }
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
    // P-27: masks read side by side give the reading the one-by-one order gave, in less time.
    void parallelAttemptsKeepTheSequentialReading() {
        if (qEnvironmentVariableIsEmpty("SUBTITLE_TEST_TESSERACT")) QSKIP("Actual Tesseract not configured");
        const QDir dir(QStringLiteral(OCR_FIXTURE_DIR));
        int withMasks = 0;
        for (const char* file : {"outlined-en-18px.png", "outlined-ko-18px.png", "outlined-ja-22px.png",
                                 "shadow-en-14px.png", "shadow-ko-14px.png", "shadow-ja-18px.png"}) {
            const QImage image(dir.filePath(QString::fromLatin1(file)));
            QVERIFY2(!image.isNull(), file);
            OcrService service;
            PaddleOcr::setThreads(1); // the attempt budget: one at a time
            QElapsedTimer timer; timer.start();
            const QString oneByOne = readWith(service, image);
            const int used = service.attemptsUsed();
            const qint64 oneByOneMs = timer.elapsed();
            PaddleOcr::setThreads(4);
            timer.restart();
            const QString sideBySide = readWith(service, image);
            const qint64 sideBySideMs = timer.elapsed();
            PaddleOcr::setThreads(0);
            qInfo("%s: %d attempts, one by one %lld ms, side by side %lld ms", file, used, oneByOneMs, sideBySideMs);
            QCOMPARE(sideBySide, oneByOne);
            QCOMPARE(service.attemptsUsed(), used);
            withMasks += used > 1;
        }
        QVERIFY(withMasks > 0); // at least one image needed the masks, so attempts really ran together
    }
    void cleanTextFixesStandaloneI() {
        QCOMPARE(OcrService::cleanText("| told you, didn't |?"), QString("I told you, didn't I?"));
        QCOMPARE(OcrService::cleanText("a|b"), QString("a|b"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("その扉")), QStringLiteral("その扉"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("今日 はいい天気です 。")), QStringLiteral("今日はいい天気です。"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("言ったでしょう ?")), QStringLiteral("言ったでしょう?"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("오늘은 날씨가 좋습니다.")), QStringLiteral("오늘은 날씨가 좋습니다."));
        QCOMPARE(OcrService::cleanText(QStringLiteral("HP 回復")), QStringLiteral("HP 回復")); // Latin next to Han keeps its space
        // PaddleOCR reads "|" as a divider correctly; only Tesseract readings get the "I" repair.
        QCOMPARE(OcrService::cleanText(QStringLiteral("HP | MP | SP"), false), QStringLiteral("HP | MP | SP"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("HP | MP | SP")), QStringLiteral("HP I MP I SP"));
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
        const QImage image = rendered(QStringLiteral("Missing OCR engine"), QStringLiteral("Segoe UI"));
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
