#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>
class QNetworkReply;
class QNetworkRequest;
class TranslationService final : public QObject {
    Q_OBJECT
public:
    explicit TranslationService(QObject* parent = nullptr);
    ~TranslationService() override;
    // Sends every piece in one request; results come back in the same order.
    // targetLang: DeepL code such as "KO" or "EN-US". An empty sourceLang lets DeepL detect it.
    void translate(const QStringList& texts, const QString& apiKey, bool freeApi,
                   const QString& targetLang = QStringLiteral("KO"), const QString& sourceLang = {});
    static QByteArray requestBody(const QStringList& texts, const QString& targetLang, const QString& sourceLang);
    static constexpr qsizetype MaxTexts = 50; // DeepL's limit per request
    // GET /v2/usage: sends only the key, consumes no characters.
    void checkUsage(const QString& apiKey, bool freeApi);
    void cancel();
    bool isBusy() const { return reply_ != nullptr; }
    static QStringList decodeResponses(int httpStatus, const QByteArray& body, qsizetype expected, QString& error,
                                       QString* detectedLanguage = nullptr);
    static QString decodeResponse(int httpStatus, const QByteArray& body, QString& error,
                                  QString* detectedLanguage = nullptr);
    // limit is -1 when the account does not report one.
    static bool decodeUsage(int httpStatus, const QByteArray& body, qint64& used, qint64& limit, QString& error);
    // DeepL API Free keys end with ":fx".
    static bool isFreeKey(const QString& apiKey) { return apiKey.trimmed().endsWith(QStringLiteral(":fx")); }
    static constexpr qsizetype MaxResponseBytes = 1024 * 1024;
    // HTTP status of the last finished request (0: none or transport error), and its Retry-After
    // header in seconds (-1 when absent). The app pauses or stops auto translation on 429 and 456.
    int lastHttpStatus() const { return lastStatus_; }
    int lastRetryAfterSeconds() const { return lastRetryAfter_; }
    // Tests only: send requests to a local HTTP server and shorten the time limit. The DeepL address
    // stays fixed for users (no setting, environment variable or CLI option reaches these).
    void setEndpointForTests(const QUrl& base) { testEndpoint_ = base; }
    void setTimeoutForTests(int milliseconds) { timeoutMs_ = milliseconds; }
signals:
    void translated(QStringList texts, QString detectedLanguage);
    void usageChecked(qint64 used, qint64 limit);
    void failed(QString error);
private:
    enum class Kind { Translate, Usage };
    void start(QNetworkReply* reply, Kind kind);
    QNetworkRequest request(bool freeApi, const char* path, const QString& key) const;
    QUrl testEndpoint_;
    int timeoutMs_ = 30000;
    int lastStatus_ = 0, lastRetryAfter_ = -1;
    void failCurrent(const QString& error);
    bool readAvailable();
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QTimer deadline_;
    QByteArray response_;
    qsizetype expected_ = 1;
};
