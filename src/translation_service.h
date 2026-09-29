#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
class QNetworkReply;
class TranslationService final : public QObject {
    Q_OBJECT
public:
    explicit TranslationService(QObject* parent = nullptr);
    ~TranslationService() override;
    void translate(const QString& text, const QString& apiKey, bool freeApi);
    void cancel();
    bool isBusy() const { return reply_ != nullptr; }
    static QString decodeResponse(int httpStatus, const QByteArray& body, QString& error);
    static constexpr qsizetype MaxResponseBytes = 1024 * 1024;
signals:
    void translated(QString text);
    void failed(QString error);
private:
    void failCurrent(const QString& error);
    bool readAvailable();
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QTimer deadline_;
    QByteArray response_;
};
