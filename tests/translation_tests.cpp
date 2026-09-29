#include "languages.h"
#include "subtitle/text.h"
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
        QCOMPARE(Languages::sourceForScripts(subtitle::Han, "jpn+eng+kor"), QString("JA"));
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
