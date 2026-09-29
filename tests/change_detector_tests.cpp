#include "change_detector.h"
#include <QPainter>
#include <QtTest>
using Decision = ChangeDetector::Decision;
namespace {
// Synthetic dialogue line; only the change decisions are checked, never OCR.
QImage line(const QString& text, int boxX = -1) {
    QImage image(1000, 60, QImage::Format_RGB32);
    image.fill(QColor(240, 220, 225));
    QPainter painter(&image);
    QFont font("Yu Gothic"); font.setPixelSize(36); painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(image.rect().adjusted(20, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
    if (boxX >= 0) painter.fillRect(boxX, 0, 60, 60, Qt::darkBlue); // moving background element
    painter.end();
    return image;
}
}
class ChangeChecks : public QObject {
    Q_OBJECT
private slots:
    void staticLineIsProcessedOnce() {
        ChangeDetector detector;
        const QImage frame = line(QStringLiteral("今日はいい天気です。"));
        QCOMPARE(detector.feed(frame, 0), Decision::Wait);
        QCOMPARE(detector.feed(frame, 400), Decision::Wait);
        QCOMPARE(detector.feed(frame, 800), Decision::Process);
        for (int i = 3; i < 20; ++i) QCOMPARE(detector.feed(frame, i * 400), Decision::Unchanged);
    }
    void typewriterTextWaitsUntilComplete() {
        ChangeDetector detector;
        const QString full = QStringLiteral("今日はいい天気です。");
        qint64 now = 0;
        for (int n = 1; n <= full.size(); ++n, now += 200)
            QCOMPARE(detector.feed(line(full.left(n)), now), Decision::Wait);
        QCOMPARE(detector.feed(line(full), now), Decision::Wait);
        QCOMPARE(detector.feed(line(full), now + 400), Decision::Process);
    }
    void nextLineIsProcessedAgain() {
        ChangeDetector detector;
        const QImage first = line(QStringLiteral("今日はいい天気です。"));
        const QImage second = line(QStringLiteral("明日は雨が降ります。"));
        detector.feed(first, 0); detector.feed(first, 400);
        QCOMPARE(detector.feed(first, 800), Decision::Process);
        QCOMPARE(detector.feed(second, 1200), Decision::Wait);
        QCOMPARE(detector.feed(second, 1600), Decision::Wait);
        QCOMPARE(detector.feed(second, 2000), Decision::Process);
    }
    void oneNewCharacterIsAChange() {
        QVERIFY(ChangeDetector::differs(ChangeDetector::signature(line(QStringLiteral("今日はいい天気"))),
                                        ChangeDetector::signature(line(QStringLiteral("今日はいい天気で")))));
    }
    void singlePixelNoiseIsIgnored() {
        const QImage a = line(QStringLiteral("今日はいい天気です。"));
        QImage b = a; b.setPixel(500, 5, qRgb(0, 0, 0));
        QVERIFY(!ChangeDetector::differs(ChangeDetector::signature(a), ChangeDetector::signature(b)));
    }
    void animatedBackgroundIsProcessedAtMostEveryMaxWait() {
        ChangeDetector detector;
        QList<qint64> processed;
        for (int i = 0; i <= 17; ++i) {
            const qint64 now = i * 400;
            if (detector.feed(line(QStringLiteral("今日はいい天気です。"), (i * 50) % 900), now) == Decision::Process)
                processed.append(now);
        }
        QCOMPARE(processed, (QList<qint64>{3200, 6400}));
    }
    void resetForgetsTheProcessedLine() {
        ChangeDetector detector;
        const QImage frame = line(QStringLiteral("今日はいい天気です。"));
        detector.feed(frame, 0); detector.feed(frame, 400);
        QCOMPARE(detector.feed(frame, 800), Decision::Process);
        detector.reset();
        detector.feed(frame, 1200); detector.feed(frame, 1600);
        QCOMPARE(detector.feed(frame, 2000), Decision::Process);
    }
};
QTEST_MAIN(ChangeChecks)
#include "change_detector_tests.moc"
