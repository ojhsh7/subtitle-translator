#pragma once
#include "translator.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
class QNetworkReply;
// DeepL API v2 implementation of ITranslator.
class TranslationService final : public ITranslator {
    Q_OBJECT
public:
    // An injected manager must outlive this service; production uses the owned manager.
    explicit TranslationService(QObject* parent = nullptr, QNetworkAccessManager* transport = nullptr);
    int lastHttpStatus() const { return lastHttpStatus_; }
    bool requiresUserAction() const override {
        return lastHttpStatus_==401 || lastHttpStatus_==403 || lastHttpStatus_==429 || lastHttpStatus_==456;
    }
    bool retryable() const override {
        return lastHttpStatus_ == 429 || lastHttpStatus_ >= 500 || (lastHttpStatus_ == 0 && transportFailure_);
    }
    ~TranslationService() override;
    QString name() const override { return QStringLiteral("DeepL"); }
    // Key and plan used by translateTexts(); the explicit translate() overload below keeps its own.
    void setCredentials(const QString& apiKey, bool freeApi) { apiKey_ = apiKey; freeApi_ = freeApi; }
    bool ready() const override;
    void translateTexts(const QStringList& texts, const TranslateOptions& options) override;
    // Sends every piece in one request; results come back in the same order.
    // targetLang: DeepL code such as "KO" or "EN-US". An empty sourceLang lets DeepL detect it.
    void translate(const QStringList& texts, const QString& apiKey, bool freeApi,
                   const QString& targetLang = QStringLiteral("KO"), const QString& sourceLang = {});
    void translate(const QStringList& texts, const QString& apiKey, bool freeApi, const TranslateOptions& options);
    static QByteArray requestBody(const QStringList& texts, const QString& targetLang, const QString& sourceLang);
    static QByteArray requestBody(const QStringList& texts, const TranslateOptions& options);
    static constexpr qsizetype MaxTexts = 50; // DeepL's limit per request
    // GET /v2/usage: sends only the key, consumes no characters.
    void checkUsage(const QString& apiKey, bool freeApi);
    void cancel() override;
    bool isBusy() const override { return reply_ != nullptr; }
    static QStringList decodeResponses(int httpStatus, const QByteArray& body, qsizetype expected, QString& error,
                                       QString* detectedLanguage = nullptr);
    static QString decodeResponse(int httpStatus, const QByteArray& body, QString& error,
                                  QString* detectedLanguage = nullptr);
    // limit is -1 when the account does not report one.
    static bool decodeUsage(int httpStatus, const QByteArray& body, qint64& used, qint64& limit, QString& error);
    // DeepL API Free keys end with ":fx".
    static bool isFreeKey(const QString& apiKey) { return apiKey.trimmed().endsWith(QStringLiteral(":fx")); }
    static constexpr qsizetype MaxResponseBytes = 1024 * 1024;
signals:
    void usageChecked(qint64 used, qint64 limit);
private:
    friend class ProtocolChecks;
    friend class WindowChecks;
    friend class ServiceChecks;
    enum class Kind { Translate, Usage };
    void start(QNetworkReply* reply, Kind kind);
    void failCurrent(const QString& error);
    bool readAvailable();
    QNetworkAccessManager network_;
    QNetworkAccessManager* transport_;
    int lastHttpStatus_ = 0;
    bool transportFailure_ = false;
    int requestTimeoutMs_ = 30000;
    QNetworkReply* reply_ = nullptr;
    QTimer deadline_;
    QByteArray response_;
    qsizetype expected_ = 1;
    QString apiKey_;
    bool freeApi_ = true;
};
