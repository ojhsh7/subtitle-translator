#include "ocr_service.h"
#include "capture_service.h"
#include <QtTest>
#include <QSignalSpy>
#include <QPainter>
#include <QFileInfo>
#include <QScreen>
#include <QApplication>
#include <QLabel>
class LocalChecks : public QObject {
    Q_OBJECT
private slots:
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
