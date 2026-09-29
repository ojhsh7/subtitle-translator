#include "languages.h"
#include "translation_service.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
class ProtocolChecks : public QObject {
    Q_OBJECT
private slots:
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
        const auto detect = QJsonDocument::fromJson(TranslationService::requestBody("fixture", "KO", {})).object();
        QCOMPARE(detect.value("target_lang").toString(),QString("KO"));
        QVERIFY(!detect.contains("source_lang")); // DeepL detects the language.
        const auto fixed = QJsonDocument::fromJson(TranslationService::requestBody("fixture", "EN-US", "JA")).object();
        QCOMPARE(fixed.value("source_lang").toString(),QString("JA"));
        QCOMPARE(fixed.value("target_lang").toString(),QString("EN-US"));
        QCOMPARE(fixed.value("text").toArray().first().toString(),QString("fixture"));
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
        QVERIFY(!Languages::isOcrCode("fra"));
        QVERIFY(Languages::isTargetCode("EN-US")); QVERIFY(!Languages::isTargetCode("EN-XX"));
        QCOMPARE(Languages::deeplSource("jpn"),QString("JA"));
        QCOMPARE(Languages::deeplSource("chi_tra"),QString("ZH"));
        QVERIFY(Languages::deeplSource("jpn+eng+kor").isEmpty());
        QVERIFY(Languages::sameLanguage("EN","EN-US")); QVERIFY(Languages::sameLanguage("KO","KO"));
        QVERIFY(!Languages::sameLanguage("JA","KO")); QVERIFY(!Languages::sameLanguage("","KO"));
        QCOMPARE(Languages::languageName("EN-US"),QStringLiteral("영어"));
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
