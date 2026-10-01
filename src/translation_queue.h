#pragma once
#include "translator.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <functional>
class UsageMeter;

// Shared translation front end for the screen, text and file modes:
// - cache of recent sentences per language pair (a rewound video or repeated line costs nothing),
// - requests split by DeepL's limits (50 texts, size), identical texts sent once,
// - up to two requests at a time, live screen work ahead of long batch jobs,
// - batch parts retried after 1, 2 and 4 s when the failure may pass (429, 5xx, timeout),
// - usage recorded per successful request and the session cap enforced before sending.
class TranslationQueue final : public QObject {
    Q_OBJECT
public:
    using Factory = std::function<ITranslator*(QObject* parent)>;
    TranslationQueue(Factory factory, UsageMeter* usage, QObject* parent = nullptr);
    ~TranslationQueue() override;
    // Screen modes. A live job still waiting for a translator is dropped when a newer job of the
    // same channel arrives (the screen already shows the newer line). No automatic retry.
    quint64 translateLive(const QStringList& texts, const TranslateOptions& options, const QString& channel);
    // Files and long text: any number of texts, sent in order with bounded retries.
    quint64 translateBatch(const QStringList& texts, const TranslateOptions& options);
    void cancel(quint64 id);
    void cancelAll();
    bool busy() const { return !jobs_.isEmpty(); }
    void setMaxParallel(int requests);
    void setRetryDelays(const QList<int>& delaysMs) { retryDelays_ = delaysMs; }
    void clearCache();
    int cachedCount() const { return int(cache_.size()); }
    bool lookup(const QString& text, const TranslateOptions& options, QString* translation) const;
    // Ranges [first, last) of texts per request within both limits. A text over the byte limit
    // gets a range of its own (the caller must split it before; the request is then refused).
    static QList<QPair<int, int>> plan(const QStringList& texts, int maxTexts, qsizetype maxBytes);
    static constexpr int MaxTexts = 50;
    static constexpr qsizetype MaxRequestBytes = 30000;
    static constexpr int CacheLimit = 2000;
signals:
    void finished(quint64 id, QStringList translations, QString detectedLanguage, bool fromCache);
    void progress(quint64 id, int done, int total);
    void failed(quint64 id, QString error, bool userActionRequired);
    void dropped(quint64 id);
private:
    enum class Kind { Live, Batch };
    struct Job {
        quint64 id = 0;
        Kind kind = Kind::Live;
        QString channel;
        TranslateOptions options;
        QStringList texts;
        QStringList results;
        QStringList pending;              // distinct texts still to translate
        QHash<QString, QList<int>> where; // pending text -> positions in texts
        QList<QPair<int, int>> chunks;    // over `pending`
        int nextChunk = 0;
        int retries = 0;
        bool sending = false;
        bool waiting = false;             // retry timer running
        int done = 0;
        QString detected;
    };
    struct Worker { ITranslator* translator = nullptr; quint64 job = 0; int chunk = -1; };
    quint64 submit(Kind kind, const QStringList& texts, const TranslateOptions& options, const QString& channel);
    void pump();
    void send(Worker& worker, Job& job);
    void succeeded(Worker& worker, const QStringList& translations, const QString& detected);
    void failedRequest(Worker& worker, const QString& error);
    Job* job(quint64 id);
    void finish(quint64 id, bool fromCache);
    void fail(quint64 id, const QString& error, bool userAction);
    static QString key(const QString& text, const TranslateOptions& options);
    void remember(const QString& key, const QString& translation);
    Factory factory_;
    UsageMeter* usage_;
    QList<Job> jobs_;
    QList<Worker> workers_;
    int maxParallel_ = 2;
    QList<int> retryDelays_{1000, 2000, 4000};
    quint64 nextId_ = 0;
    QHash<QString, QString> cache_;
    QStringList cacheOrder_;
};
