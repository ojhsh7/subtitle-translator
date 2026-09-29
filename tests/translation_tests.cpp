#include "translation_service.h"
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
};
QTEST_GUILESS_MAIN(ProtocolChecks)
#include "translation_tests.moc"
