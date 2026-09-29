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
    // targetLang: DeepL code such as "KO" or "EN-US". An empty sourceLang lets DeepL detect it.
    void translate(const QString& text, const QString& apiKey, bool freeApi,
                   const QString& targetLang = QStringLiteral("KO"), const QString& sourceLang = {});
    static QByteArray requestBody(const QString& text, const QString& targetLang, const QString& sourceLang);
    // GET /v2/usage: sends only the key, consumes no characters.
    void checkUsage(const QString& apiKey, bool freeApi);
    void cancel();
    bool isBusy() const { return reply_ != nullptr; }
    static QString decodeResponse(int httpStatus, const QByteArray& body, QString& error,
                                  QString* detectedLanguage = nullptr);
    // limit is -1 when the account does not report one.
    static bool decodeUsage(int httpStatus, const QByteArray& body, qint64& used, qint64& limit, QString& error);
    // DeepL API Free keys end with ":fx".
    static bool isFreeKey(const QString& apiKey) { return apiKey.trimmed().endsWith(QStringLiteral(":fx")); }
    static constexpr qsizetype MaxResponseBytes = 1024 * 1024;
signals:
    void translated(QString text, QString detectedLanguage);
    void usageChecked(qint64 used, qint64 limit);
    void failed(QString error);
private:
    enum class Kind { Translate, Usage };
    void start(QNetworkReply* reply, Kind kind);
    void failCurrent(const QString& error);
    bool readAvailable();
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QTimer deadline_;
    QByteArray response_;
};
