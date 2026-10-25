#include "languages.h"
#include "local_http.h"
#include "subtitle/text.h"
#include "translation_service.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>
namespace {
// Real HTTP through QNetworkAccessManager against tests/local_http.h (P-04); never DeepL.
// (Kept outside the test class, and no raw string literals: moc does not parse them.)
struct Run {
    QSignalSpy translated, failed;
    explicit Run(TranslationService& service)
        : translated(&service, &TranslationService::translated), failed(&service, &TranslationService::failed) {}
    bool wait(int ms = 5000) { return QTest::qWaitFor([&] { return translated.count() + failed.count() > 0; }, ms); }
    QString error() const { return failed.isEmpty() ? QString() : failed.first().first().toString(); }
};
}
class ProtocolChecks : public QObject {
    Q_OBJECT
private slots:
    void localHttpSuccessUsesHeaderAuthAndOneRequest() {
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"translations\":[{\"detected_source_language\":\"JA\",\"text\":\"날씨\"},{\"text\":\"좋다\"}]}"};
        TranslationService service; service.setEndpointForTests(http.url());
        Run run(service);
        service.translate({QStringLiteral("天気"), QStringLiteral("いい")}, QStringLiteral("test-key:fx"), true, "KO", "JA");
        QVERIFY(service.isBusy());
        QVERIFY(run.wait());
        QCOMPARE(int(run.translated.count()), 1);
        QCOMPARE(run.translated.first().at(0).toStringList(), QStringList({QStringLiteral("날씨"), QStringLiteral("좋다")}));
        QCOMPARE(run.translated.first().at(1).toString(), QStringLiteral("JA"));
        QCOMPARE(int(http.requests.size()), 1);
        const QByteArray request = http.requests.first();
        QVERIFY(request.startsWith("POST /v2/translate "));
        QVERIFY(request.toLower().contains("authorization: deepl-auth-key test-key:fx")); // Qt 6 sends lower-case names
        QVERIFY(request.contains("\"source_lang\":\"JA\""));
        QCOMPARE(service.lastHttpStatus(), 200);
    }
    void localHttpErrorStatusesAreReported_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("headers");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("retryAfter");
        QTest::newRow("401") << 401 << QByteArray() << QStringLiteral("인증") << -1;
        QTest::newRow("429 retry-after") << 429 << QByteArray("Retry-After: 7\r\n") << QStringLiteral("너무 많") << 7;
        QTest::newRow("429 bare") << 429 << QByteArray() << QStringLiteral("너무 많") << -1;
        QTest::newRow("456") << 456 << QByteArray() << QStringLiteral("한도") << -1;
        QTest::newRow("500") << 500 << QByteArray() << QStringLiteral("서버 오류") << -1;
        QTest::newRow("302") << 302 << QByteArray("Location: http://127.0.0.1:1/\r\n") << QStringLiteral("주소 이동") << -1;
    }
    void localHttpErrorStatusesAreReported() {
        QFETCH(int, status); QFETCH(QByteArray, headers); QFETCH(QString, message); QFETCH(int, retryAfter);
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{status, "private-server-body", headers};
        TranslationService service; service.setEndpointForTests(http.url());
        Run run(service);
        service.translate({QStringLiteral("天気")}, QStringLiteral("test-key"), false);
        QVERIFY(run.wait());
        QCOMPARE(int(run.translated.count()), 0);
        QVERIFY2(run.error().contains(message), qPrintable(run.error()));
        QVERIFY(!run.error().contains("private-server-body"));
        QCOMPARE(service.lastHttpStatus(), status);
        QCOMPARE(service.lastRetryAfterSeconds(), retryAfter);
        QCOMPARE(int(http.requests.size()), 1); // no automatic retry
    }
    void localHttpBrokenBodiesNeverBecomeTranslations() {
        for (const LocalHttp::Reply& reply : {LocalHttp::Reply{200, "{\"translations\":[{\"text\":\"부분", {}, false, true},
                                              LocalHttp::Reply{200, "{\"translations\":[{\"text\":\"하나\"}]}"},
                                              LocalHttp::Reply{200, "<html>proxy</html>"}}) {
            LocalHttp http; QVERIFY(http.listening());
            http.replies << reply;
            TranslationService service; service.setEndpointForTests(http.url());
            Run run(service);
            service.translate({QStringLiteral("一"), QStringLiteral("二")}, QStringLiteral("test-key"), false);
            QVERIFY(run.wait());
            QCOMPARE(int(run.translated.count()), 0); // truncated, count mismatch, not JSON
            QVERIFY(!run.error().isEmpty());
        }
    }
    void localHttpTimeoutAndCancel() {
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, {}, {}, true} << LocalHttp::Reply{200, {}, {}, true};
        TranslationService service; service.setEndpointForTests(http.url()); service.setTimeoutForTests(800);
        Run timeout(service);
        QElapsedTimer timer; timer.start();
        service.translate({QStringLiteral("天気")}, QStringLiteral("test-key"), false);
        QVERIFY(timeout.wait(5000));
        QVERIFY2(timeout.error().contains(QStringLiteral("초과")), qPrintable(timeout.error()));
        QVERIFY(timer.elapsed() < 3000);
        QVERIFY(!service.isBusy());
        Run cancelled(service);
        service.translate({QStringLiteral("天気")}, QStringLiteral("test-key"), false);
        QTRY_COMPARE(int(http.requests.size()), 2);
        service.cancel();
        QVERIFY(!service.isBusy());
        QTest::qWait(1500); // beyond the time limit: a cancelled request reports nothing
        QCOMPARE(int(cancelled.translated.count() + cancelled.failed.count()), 0);
    }
    void localHttpUsageCheck() {
        LocalHttp http; QVERIFY(http.listening());
        http.replies << LocalHttp::Reply{200, "{\"character_count\":5,\"character_limit\":500000}"};
        TranslationService service; service.setEndpointForTests(http.url());
        QSignalSpy usage(&service, &TranslationService::usageChecked);
        service.checkUsage(QStringLiteral("test-key:fx"), true);
        QTRY_COMPARE(int(usage.count()), 1);
        QCOMPARE(usage.first().at(0).toLongLong(), 5LL);
        QVERIFY(http.requests.first().startsWith("GET /v2/usage "));
    }
    void artificialResponseFixture() {
        // This is a protocol fixture, never a live translation success.
        QString error;
        const auto text = TranslationService::decodeResponse(200,
            QByteArrayLiteral("{\"translations\":[{\"text\":\"fixture\"}]}"),error);
        QCOMPARE(text,QString("fixture")); QVERIFY(error.isEmpty());
    }
    void rejectedPayloads() {
        for(const QByteArray body : {QByteArray("{}"),QByteArray("html"),QByteArray("{\"translations\":[{\"text\":\"\"}]}")}) {
            QString error; QVERIFY(TranslationService::decodeResponse(200,body,error).isEmpty()); QVERIFY(!error.isEmpty());
        }
    }
    void errorResponseNeverExposed() {
        for(int code : {301,403,429,456,500}) {
            QString error;
            QVERIFY(TranslationService::decodeResponse(code,"private-server-body",error).isEmpty());
            QVERIFY(!error.contains("private-server-body")); QVERIFY(!error.isEmpty());
        }
    }
    void responseLimit() {
        QString error;
        QVERIFY(TranslationService::decodeResponse(200,QByteArray(1024*1024+1,' '),error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void usageFixture() {
        // Protocol fixture shaped like DeepL's documented example, never a live key check.
        qint64 used = -1, limit = -1; QString error;
        QVERIFY(TranslationService::decodeUsage(200,
            QByteArrayLiteral("{\"character_count\":180118,\"character_limit\":1250000}"),used,limit,error));
        QCOMPARE(used,qint64(180118)); QCOMPARE(limit,qint64(1250000)); QVERIFY(error.isEmpty());
        QVERIFY(TranslationService::decodeUsage(200,QByteArrayLiteral("{\"character_count\":5}"),used,limit,error));
        QCOMPARE(used,qint64(5)); QCOMPARE(limit,qint64(-1));
    }
    void rejectedUsage() {
        for(const QByteArray body : {QByteArray("{}"),QByteArray("html"),QByteArray("{\"character_count\":-1}"),
                                     QByteArray("{\"character_count\":\"5\"}"),QByteArray("{\"character_count\":1.5}")}) {
            qint64 used, limit; QString error;
            QVERIFY(!TranslationService::decodeUsage(200,body,used,limit,error)); QVERIFY(!error.isEmpty());
        }
        for(int code : {403,429,456,500}) {
            qint64 used, limit; QString error;
            QVERIFY(!TranslationService::decodeUsage(code,"private-server-body",used,limit,error));
            QVERIFY(!error.contains("private-server-body")); QVERIFY(!error.isEmpty());
        }
    }
    void requestBodyTargetAndOptionalSource() {
        const auto detect = QJsonDocument::fromJson(TranslationService::requestBody({"fixture"}, "KO", {})).object();
        QCOMPARE(detect.value("target_lang").toString(),QString("KO"));
        QVERIFY(!detect.contains("source_lang")); // DeepL detects the language.
        const auto fixed = QJsonDocument::fromJson(TranslationService::requestBody({"fixture"}, "EN-US", "JA")).object();
        QCOMPARE(fixed.value("source_lang").toString(),QString("JA"));
        QCOMPARE(fixed.value("target_lang").toString(),QString("EN-US"));
        QCOMPARE(fixed.value("text").toArray().first().toString(),QString("fixture"));
    }
    void severalPiecesInOneRequest() {
        const auto body = QJsonDocument::fromJson(TranslationService::requestBody({"Hello", "Bye"}, "KO", {})).object();
        QCOMPARE(body.value("text").toArray().size(), 2);
        QString error, detected;
        const auto results = TranslationService::decodeResponses(200, QByteArrayLiteral(
            "{\"translations\":[{\"detected_source_language\":\"EN\",\"text\":\"a\"},{\"detected_source_language\":\"EN\",\"text\":\"b\"}]}"),
            2, error, &detected);
        QCOMPARE(results, (QStringList{"a", "b"})); QVERIFY(error.isEmpty()); QCOMPARE(detected, QString("EN"));
        // A count mismatch must fail instead of shifting pieces into the wrong place.
        QVERIFY(TranslationService::decodeResponses(200, QByteArrayLiteral("{\"translations\":[{\"text\":\"a\"}]}"), 2, error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void detectedLanguageFixture() {
        QString error, detected;
        TranslationService::decodeResponse(200,
            QByteArrayLiteral("{\"translations\":[{\"detected_source_language\":\"EN\",\"text\":\"fixture\"}]}"),error,&detected);
        QCOMPARE(detected,QString("EN")); QVERIFY(error.isEmpty());
        TranslationService::decodeResponse(200,
            QByteArrayLiteral("{\"translations\":[{\"detected_source_language\":\"<b>x</b>\",\"text\":\"fixture\"}]}"),error,&detected);
        QVERIFY(detected.isEmpty()); // Only well-formed codes reach the UI.
    }
    void languageTables() {
        QVERIFY(Languages::isOcrCode("jpn+eng+kor"));
        QVERIFY(!Languages::isOcrCode("jpn -c tessedit_write_images=1"));
        QVERIFY(Languages::isOcrCode("fra"));
        QVERIFY(!Languages::isOcrCode("unsupported"));
        QVERIFY(Languages::isTargetCode("EN-US")); QVERIFY(!Languages::isTargetCode("EN-XX"));
        QCOMPARE(Languages::deeplSource("jpn"),QString("JA"));
        QCOMPARE(Languages::deeplSource("chi_tra"),QString("ZH"));
        QVERIFY(Languages::deeplSource("jpn+eng+kor").isEmpty());
        QVERIFY(Languages::sameLanguage("EN","EN-US")); QVERIFY(Languages::sameLanguage("KO","KO"));
        QVERIFY(!Languages::sameLanguage("JA","KO")); QVERIFY(!Languages::sameLanguage("","KO"));
        QCOMPARE(Languages::languageName("EN-US"),QStringLiteral("영어"));
        // Short fragments get an explicit source language when the script leaves no doubt.
        QCOMPARE(Languages::sourceForScripts(subtitle::Latin, "jpn+eng+kor"), QString("EN"));
        QCOMPARE(Languages::sourceForScripts(subtitle::Kana | subtitle::Han, "jpn+eng+kor"), QString("JA"));
        QVERIFY(Languages::sourceForScripts(subtitle::Han, "jpn+eng+kor").isEmpty());
        QCOMPARE(Languages::sourceForScripts(subtitle::Han, "jpn"), QString("JA"));
        QCOMPARE(Languages::sourceForScripts(subtitle::Han, "chi_sim"), QString("ZH"));
        QVERIFY(Languages::sourceForScripts(subtitle::Han, "jpn+eng+kor+chi_sim").isEmpty()); // kanji or hanzi
        QVERIFY(Languages::sourceForScripts(subtitle::Latin | subtitle::Kana, "jpn+eng+kor").isEmpty()); // mixed
        QCOMPARE(Languages::sourceForScripts(subtitle::Latin, "jpn"), QString("JA")); // a fixed choice wins
        QVERIFY(Languages::sourceForScripts(subtitle::Latin, "multi").isEmpty());
        QVERIFY(Languages::sourceForScripts(subtitle::Han, "multi").isEmpty());
        QCOMPARE(Languages::sourceForScripts(subtitle::Latin, "fra"), QString("FR"));
        QCOMPARE(Languages::sourceForScripts(subtitle::Cyrillic, "rus"), QString("RU"));
        QCOMPARE(Languages::sourceForScripts(subtitle::Arabic, "ara"), QString("AR"));
        QCOMPARE(Languages::sourceForScripts(subtitle::Devanagari, "hin"), QString("HI"));
    }
    void freeKeySuffix() {
        QVERIFY(TranslationService::isFreeKey("fixture-key:fx"));
        QVERIFY(TranslationService::isFreeKey(" fixture-key:fx \n"));
        QVERIFY(!TranslationService::isFreeKey("fixture-key"));
        QVERIFY(!TranslationService::isFreeKey("fixture-key:fxx"));
    }
};
QTEST_GUILESS_MAIN(ProtocolChecks)
#include "translation_tests.moc"
