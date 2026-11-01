#include "change_detector.h"
#include <QPainter>
#include <QtTest>
using Decision = ChangeDetector::Decision;
namespace {
// The app polls every PollMs; a still frame is read on the second quiet poll.
constexpr qint64 P = ChangeDetector::PollMs;
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
    void settleIsTwoPolls() {
        QCOMPARE(ChangeDetector::SettleMs, 2 * P);
        QVERIFY(ChangeDetector::SentenceSettleMs > ChangeDetector::SettleMs);
    }
    void staticLineIsProcessedOnce() {
        ChangeDetector detector;
        const QImage frame = line(QStringLiteral("今日はいい天気です。"));
        QCOMPARE(detector.feed(frame, 0), Decision::Wait);
        QCOMPARE(detector.stableMs(0), qint64(0));
        QCOMPARE(detector.feed(frame, P), Decision::Wait);
        QCOMPARE(detector.feed(frame, 2 * P), Decision::Process);
        QCOMPARE(detector.stableMs(2 * P), 2 * P);
        for (int i = 3; i < 20; ++i) QCOMPARE(detector.feed(frame, i * P), Decision::Unchanged);
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
    // P-08 (R-05): reads per sentence for typewriter text, polled like the app. A sentence typed for
    // longer than MaxWaitMs is read once while still typing and once when complete; shorter ones once.
    void typewriterReadsPerSentence_data() {
        QTest::addColumn<int>("characters");
        QTest::addColumn<int>("msPerCharacter");
        QTest::addColumn<int>("partialReads");
        QTest::newRow("20 chars x 100 ms (2 s)") << 20 << 100 << 0;
        QTest::newRow("28 chars x 100 ms (2.8 s)") << 28 << 100 << 0;
        QTest::newRow("40 chars x 100 ms (4 s)") << 40 << 100 << 1;
        QTest::newRow("40 chars x 200 ms (8 s)") << 40 << 200 << 2;
    }
    void typewriterReadsPerSentence() {
        QFETCH(int, characters); QFETCH(int, msPerCharacter); QFETCH(int, partialReads);
        const QString kana = QStringLiteral("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん");
        // 40 characters must fit the frame, or the clipped tail never changes the picture.
        auto line = [](const QString& text) {
            QImage image(1000, 60, QImage::Format_RGB32);
            image.fill(QColor(240, 220, 225));
            QPainter painter(&image);
            QFont font("Yu Gothic"); font.setPixelSize(20); painter.setFont(font);
            painter.setPen(Qt::black);
            painter.drawText(image.rect().adjusted(20, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
            return image;
        };
        ChangeDetector detector;
        qint64 now = 0;
        int totalPartial = 0, totalComplete = 0;
        for (int sentence = 0; sentence < 3; ++sentence) {
            const QString full = kana.mid(sentence, characters);
            const qint64 start = now, typed = start + qint64(characters) * msPerCharacter, end = typed + 2500;
            int partial = 0, complete = 0;
            for (; now < end; now += P) {
                const int shown = int(std::min<qint64>(characters, (now - start) / msPerCharacter + 1));
                if (detector.feed(line(full.left(shown)), now) == Decision::Process) (shown < characters ? partial : complete)++;
            }
            QCOMPARE(complete, 1);       // the finished sentence is always read, once
            QCOMPARE(partial, partialReads);
            totalPartial += partial; totalComplete += complete;
        }
        qInfo("%d chars x %d ms: %d complete and %d partial reads in 3 sentences", characters, msPerCharacter, totalComplete, totalPartial);
    }
    void nextLineIsProcessedAgain() {
        ChangeDetector detector;
        const QImage first = line(QStringLiteral("今日はいい天気です。"));
        const QImage second = line(QStringLiteral("明日は雨が降ります。"));
        detector.feed(first, 0); detector.feed(first, P);
        QCOMPARE(detector.feed(first, 2 * P), Decision::Process);
        QCOMPARE(detector.feed(second, 3 * P), Decision::Wait);
        QCOMPARE(detector.stableMs(3 * P), qint64(0)); // the moment a new line appears (connection warm-up)
        QCOMPARE(detector.feed(second, 4 * P), Decision::Wait);
        QCOMPARE(detector.feed(second, 5 * P), Decision::Process);
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
        for (int i = 0; i <= 45; ++i) {
            const qint64 now = i * P;
            if (detector.feed(line(QStringLiteral("今日はいい天気です。"), (i * 50) % 900), now) == Decision::Process)
                processed.append(now);
        }
        QCOMPARE(processed, (QList<qint64>{ChangeDetector::MaxWaitMs, 2 * ChangeDetector::MaxWaitMs}));
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
        detector.feed(before,0); detector.feed(before,P);
        QCOMPARE(detector.feed(before,2*P),Decision::Process);
        QCOMPARE(detector.feed(after,3*P),Decision::Wait);
        QCOMPARE(detector.feed(after,4*P),Decision::Wait);
        QCOMPARE(detector.feed(after,5*P),Decision::Process);
    }
    void intermittentlyAnimatedBackgroundCannotWaitForever() {
        ChangeDetector detector;
        QList<qint64> processed;
        // Each scene frame lasts two polls: never enough stable polls, but not changing every poll.
        for (int i=0;i<=45;++i) {
            if (detector.feed(line(QStringLiteral("今日はいい天気です。"),((i/2)*50)%900),i*P)==Decision::Process)
                processed.append(i*P);
        }
        QCOMPARE(processed,(QList<qint64>{ChangeDetector::MaxWaitMs, 2 * ChangeDetector::MaxWaitMs}));
    }
    void failedOcrRetriesAreDelayedAndBounded() {
        ChangeDetector detector;
        const QImage frame=line(QStringLiteral("今日はいい天気です。"));
        detector.feed(frame,0); detector.feed(frame,P);
        QCOMPARE(detector.feed(frame,2*P),Decision::Process);
        QVERIFY(detector.retryAfterFailure(400));
        QVERIFY(!detector.retryAfterFailure(450)); // Duplicate failure cannot consume another retry.
        QCOMPARE(detector.feed(frame,3399),Decision::Unchanged);
        QCOMPARE(detector.feed(frame,3400),Decision::Process);
        QVERIFY(detector.retryAfterFailure(3500));
        QCOMPARE(detector.feed(frame,6499),Decision::Unchanged);
        QCOMPARE(detector.feed(frame,6500),Decision::Process);
        QVERIFY(!detector.retryAfterFailure(6600));
        QCOMPARE(detector.feed(frame,20000),Decision::Unchanged);
        const QImage next=line(QStringLiteral("明日は雨が降ります。"));
        detector.feed(next,20000+P); detector.feed(next,20000+2*P);
        QCOMPARE(detector.feed(next,20000+3*P),Decision::Process);
        QVERIFY(detector.retryAfterFailure(20500)); // New dialogue gets its own retry allowance.
        detector.reset();
        QVERIFY(!detector.retryAfterFailure(20600));
    }
    void forcedPartialReadIsConfirmedOnceWhenSettled() {
        ChangeDetector detector;
        QImage last;
        const int forcedAt = int(ChangeDetector::MaxWaitMs / P);
        for(int i=0;i<=forcedAt;++i) {
            last=line(QStringLiteral("今日はいい天気です。"),i*50);
            detector.feed(last,i*P);
        }
        const qint64 forced = forcedAt * P;
        QCOMPARE(detector.feed(last,forced+P),Decision::Wait);
        QCOMPARE(detector.feed(last,forced+2*P),Decision::Process);
        QCOMPARE(detector.feed(last,forced+3*P),Decision::Unchanged);
    }
    void resetForgetsTheProcessedLine() {
        ChangeDetector detector;
        const QImage frame = line(QStringLiteral("今日はいい天気です。"));
        detector.feed(frame, 0); detector.feed(frame, P);
        QCOMPARE(detector.feed(frame, 2 * P), Decision::Process);
        detector.reset();
        detector.feed(frame, 3 * P); detector.feed(frame, 4 * P);
        QCOMPARE(detector.feed(frame, 5 * P), Decision::Process);
    }
};
QTEST_MAIN(ChangeChecks)
#include "change_detector_tests.moc"
