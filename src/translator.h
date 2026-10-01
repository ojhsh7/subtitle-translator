#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

// Options of one translation request. Engines ignore what they do not support.
struct TranslateOptions {
    QString target = QStringLiteral("KO"); // DeepL-style code, e.g. "KO", "EN-US"
    QString source;                        // empty: let the engine detect it
    QString context;                       // helps the translation (previous subtitle line), not translated
    bool xmlTags = false;                  // texts hold <x>…</x> spans to keep as they are, XML-escaped
};

// A translation engine (stage 0: DeepL only). One request at a time; the results come back in the
// same order as the texts. TranslationQueue adds caching, splitting, retries and usage on top.
class ITranslator : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITranslator() override = default;
    virtual QString name() const = 0;
    virtual bool ready() const = 0; // credentials are present
    virtual void translateTexts(const QStringList& texts, const TranslateOptions& options) = 0;
    virtual void cancel() = 0;      // no signal follows a cancel
    virtual bool isBusy() const = 0;
    // The last failure needs the user (key, plan, quota, rate): automatic modes must stop.
    virtual bool requiresUserAction() const = 0;
    // The last failure may pass by itself (rate limit, server error, timeout).
    virtual bool retryable() const = 0;
signals:
    void translated(QStringList texts, QString detectedLanguage);
    void failed(QString error);
};
