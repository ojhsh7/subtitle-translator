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
    void smallSentenceEndingInsideLargeRegionIsDetected_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("japanese") << QStringLiteral("。");
        QTest::newRow("english") << QStringLiteral("!");
    }
    void smallSentenceEndingInsideLargeRegionIsDetected() {
        QFETCH(QString,text);
        QImage before(2200,420,QImage::Format_RGB32); before.fill(QColor(80,80,80));
        QImage after=before;
        QPainter p(&after);
        QFont f("Yu Gothic"); f.setPixelSize(18); p.setFont(f); p.setPen(QColor(220,220,220));
        p.drawText(QPoint(1820,260),text); p.end();
        ChangeDetector detector;
        detector.feed(before,0); detector.feed(before,400);
        QCOMPARE(detector.feed(before,800),Decision::Process);
        QCOMPARE(detector.feed(after,1200),Decision::Wait);
        QCOMPARE(detector.feed(after,1600),Decision::Wait);
        QCOMPARE(detector.feed(after,2000),Decision::Process);
    }
    void intermittentlyAnimatedBackgroundCannotWaitForever() {
        ChangeDetector detector;
        QList<qint64> processed;
        // Each scene frame lasts two polls: never enough stable polls, but not changing every poll.
        for (int i=0;i<=17;++i) {
            if (detector.feed(line(QStringLiteral("今日はいい天気です。"),((i/2)*50)%900),i*400)==Decision::Process)
                processed.append(i*400);
        }
        QCOMPARE(processed,(QList<qint64>{3200,6400}));
    }
    void failedOcrRetriesAreDelayedAndBounded() {
        ChangeDetector detector;
        const QImage frame=line(QStringLiteral("今日はいい天気です。"));
        detector.feed(frame,0); detector.feed(frame,400);
        QCOMPARE(detector.feed(frame,800),Decision::Process);
        QVERIFY(detector.retryAfterFailure(900));
        QVERIFY(!detector.retryAfterFailure(950)); // Duplicate failure cannot consume another retry.
        QCOMPARE(detector.feed(frame,3800),Decision::Unchanged);
        QCOMPARE(detector.feed(frame,3900),Decision::Process);
        QVERIFY(detector.retryAfterFailure(4000));
        QCOMPARE(detector.feed(frame,6999),Decision::Unchanged);
        QCOMPARE(detector.feed(frame,7000),Decision::Process);
        QVERIFY(!detector.retryAfterFailure(7100));
        QCOMPARE(detector.feed(frame,20000),Decision::Unchanged);
        const QImage next=line(QStringLiteral("明日は雨が降ります。"));
        detector.feed(next,20400); detector.feed(next,20800);
        QCOMPARE(detector.feed(next,21200),Decision::Process);
        QVERIFY(detector.retryAfterFailure(21300)); // New dialogue gets its own retry allowance.
        detector.reset();
        QVERIFY(!detector.retryAfterFailure(21400));
    }
    void forcedPartialReadIsConfirmedOnceWhenSettled() {
        ChangeDetector detector;
        QImage last;
        for(int i=0;i<=8;++i) {
            last=line(QStringLiteral("今日はいい天気です。"),i*50);
            detector.feed(last,i*400);
        }
        QCOMPARE(detector.feed(last,3600),Decision::Wait);
        QCOMPARE(detector.feed(last,4000),Decision::Process);
        QCOMPARE(detector.feed(last,4400),Decision::Unchanged);
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
