#include "ocr_service.h"
#include "capture_service.h"
#include <QtTest>
#include <QSignalSpy>
#include <QPainter>
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
}
class LocalChecks : public QObject {
    Q_OBJECT
private slots:
    void cleanTextFixesStandaloneI() {
        QCOMPARE(OcrService::cleanText("| told you, didn't |?"), QString("I told you, didn't I?"));
        QCOMPARE(OcrService::cleanText("a|b"), QString("a|b"));
        QCOMPARE(OcrService::cleanText(QStringLiteral("その扉")), QStringLiteral("その扉"));
    }
    void automaticLayoutReadsStripsAsOneLine() {
        QCOMPARE(OcrService::resolvePsm(0, QSize(400,42)), 7);
        QCOMPARE(OcrService::resolvePsm(0, QSize(800,120)), 6);
        QCOMPARE(OcrService::resolvePsm(6, QSize(400,42)), 6); // An explicit choice wins.
        QCOMPARE(OcrService::resolvePsm(7, QSize(800,120)), 7);
    }
    void smallLinesAreUpscaled() {
        QCOMPARE(OcrService::prepare(QImage(300,24,QImage::Format_RGB32)).size(), QSize(600,48));
        QCOMPARE(OcrService::prepare(QImage(300,60,QImage::Format_RGB32)).size(), QSize(300,60));
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
