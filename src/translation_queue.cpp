#include "translation_queue.h"
#include "usage_meter.h"
#include <QTimer>
#include <algorithm>

TranslationQueue::TranslationQueue(Factory factory, UsageMeter* usage, QObject* parent)
    : QObject(parent), factory_(std::move(factory)), usage_(usage) {}
TranslationQueue::~TranslationQueue() {
    for (auto& worker : workers_) if (worker.translator) worker.translator->cancel();
}
void TranslationQueue::setMaxParallel(int requests) { maxParallel_ = std::max(1, requests); }
QString TranslationQueue::key(const QString& text, const TranslateOptions& options) {
    return options.target + QChar(0x1f) + options.source + QChar(0x1f) + (options.xmlTags ? u'1' : u'0') + QChar(0x1f) + text;
}
void TranslationQueue::remember(const QString& k, const QString& translation) {
    if (!cache_.contains(k)) {
        cacheOrder_ << k;
        while (cacheOrder_.size() > CacheLimit) cache_.remove(cacheOrder_.takeFirst());
    }
    cache_.insert(k, translation);
}
void TranslationQueue::clearCache() { cache_.clear(); cacheOrder_.clear(); }
bool TranslationQueue::lookup(const QString& text, const TranslateOptions& options, QString* translation) const {
    const auto found = cache_.constFind(key(text, options));
    if (found == cache_.cend()) return false;
    if (translation) *translation = found.value();
    return true;
}
QList<QPair<int, int>> TranslationQueue::plan(const QStringList& texts, int maxTexts, qsizetype maxBytes) {
    QList<QPair<int, int>> ranges;
    int first = 0;
    qsizetype bytes = 0;
    for (int i = 0; i < texts.size(); ++i) {
        const qsizetype size = texts[i].toUtf8().size();
        if (i > first && (i - first >= maxTexts || bytes + size > maxBytes)) {
            ranges.append({first, i});
            first = i; bytes = 0;
        }
        bytes += size;
    }
    if (first < texts.size()) ranges.append({first, int(texts.size())});
    return ranges;
}
quint64 TranslationQueue::translateLive(const QStringList& texts, const TranslateOptions& options, const QString& channel) {
    return submit(Kind::Live, texts, options, channel);
}
quint64 TranslationQueue::translateBatch(const QStringList& texts, const TranslateOptions& options) {
    return submit(Kind::Batch, texts, options, {});
}
quint64 TranslationQueue::submit(Kind kind, const QStringList& texts, const TranslateOptions& options, const QString& channel) {
    Job job;
    job.id = ++nextId_;
    job.kind = kind;
    job.channel = channel;
    job.options = options;
    job.texts = texts;
    job.results = texts; // blank texts stay as they are
    for (int i = 0; i < texts.size(); ++i) {
        const QString& text = texts[i];
        if (text.trimmed().isEmpty()) continue;
        QString cached;
        if (lookup(text, options, &cached)) { job.results[i] = cached; continue; }
        if (!job.where.contains(text)) job.pending << text;
        job.where[text] << i;
    }
    job.chunks = plan(job.pending, MaxTexts, MaxRequestBytes);
    const quint64 id = job.id;
    if (kind == Kind::Live) {
        // The screen moved on: an older live line that has not been sent is not worth paying for.
        QList<quint64> stale;
        for (const Job& other : jobs_)
            if (other.kind == Kind::Live && other.channel == channel && !other.sending && other.done == 0) stale << other.id;
        for (quint64 old : stale) {
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [old](const Job& j) { return j.id == old; }), jobs_.end());
            emit dropped(old);
        }
    }
    const bool fromCache = job.pending.isEmpty();
    jobs_ << job;
    if (fromCache) QTimer::singleShot(0, this, [this, id] { finish(id, true); });
    else pump();
    return id;
}
TranslationQueue::Job* TranslationQueue::job(quint64 id) {
    for (Job& j : jobs_) if (j.id == id) return &j;
    return nullptr;
}
void TranslationQueue::pump() {
    for (;;) {
        Job* next = nullptr;
        for (Kind kind : {Kind::Live, Kind::Batch}) {
            for (Job& j : jobs_)
                if (j.kind == kind && !j.sending && !j.waiting && j.nextChunk < j.chunks.size()) { next = &j; break; }
            if (next) break;
        }
        if (!next) return;
        int idle = -1;
        for (int i = 0; i < workers_.size(); ++i) if (workers_[i].job == 0) { idle = i; break; }
        if (idle < 0 && workers_.size() < maxParallel_) {
            Worker worker;
            worker.translator = factory_(this);
            if (!worker.translator) { fail(next->id, QStringLiteral("번역 엔진을 준비하지 못했습니다."), true); continue; }
            const int index = int(workers_.size());
            // Queued: a translator may fail inside translateTexts(); handle it after send() returns.
            connect(worker.translator, &ITranslator::translated, this,
                    [this, index](const QStringList& texts, const QString& detected) { succeeded(workers_[index], texts, detected); },
                    Qt::QueuedConnection);
            connect(worker.translator, &ITranslator::failed, this,
                    [this, index](const QString& error) { failedRequest(workers_[index], error); }, Qt::QueuedConnection);
            workers_ << worker;
            idle = index;
        }
        if (idle < 0) return;
        send(workers_[idle], *next);
    }
}
void TranslationQueue::send(Worker& worker, Job& job) {
    const auto range = job.chunks[job.nextChunk];
    const QStringList texts = job.pending.mid(range.first, range.second - range.first);
    qsizetype bytes = 0;
    for (const QString& text : texts) bytes += text.toUtf8().size();
    if (bytes > MaxRequestBytes) {
        fail(job.id, QStringLiteral("번역하지 못했습니다 · 한 문단이 30KB보다 깁니다 · 문단을 나눠 다시 시도하세요."), false);
        return;
    }
    if (usage_ && !usage_->allows(UsageMeter::characters(texts))) {
        fail(job.id, usage_->capReachedMessage(), true);
        return;
    }
    if (!worker.translator->ready()) {
        fail(job.id, QStringLiteral("번역하지 않았습니다 · DeepL API 키가 없습니다 · 설정에서 키와 전송 동의를 확인하세요."), true);
        return;
    }
    job.sending = true;
    worker.job = job.id;
    worker.chunk = job.nextChunk;
    worker.translator->translateTexts(texts, job.options);
}
void TranslationQueue::succeeded(Worker& worker, const QStringList& translations, const QString& detected) {
    const quint64 id = worker.job;
    const int chunk = worker.chunk;
    worker.job = 0; worker.chunk = -1;
    Job* j = job(id);
    if (!j || chunk < 0 || chunk >= j->chunks.size()) { pump(); return; }
    const auto range = j->chunks[chunk];
    if (translations.size() != range.second - range.first) {
        fail(id, QStringLiteral("번역 응답 개수가 요청과 달라 결과를 쓰지 않았습니다."), false);
        pump();
        return;
    }
    QStringList sent;
    for (int k = 0; k < translations.size(); ++k) {
        const QString& text = j->pending[range.first + k];
        sent << text;
        remember(key(text, j->options), translations[k]);
        for (int position : j->where.value(text)) j->results[position] = translations[k];
    }
    if (usage_) usage_->record(UsageMeter::characters(sent));
    if (j->detected.isEmpty()) j->detected = detected;
    j->done += int(sent.size());
    j->sending = false;
    j->retries = 0;
    ++j->nextChunk;
    emit progress(id, j->done, int(j->pending.size()));
    if (j->nextChunk >= j->chunks.size()) finish(id, false);
    pump();
}
void TranslationQueue::failedRequest(Worker& worker, const QString& error) {
    const quint64 id = worker.job;
    ITranslator* translator = worker.translator;
    worker.job = 0; worker.chunk = -1;
    Job* j = job(id);
    if (!j) { pump(); return; }
    j->sending = false;
    if (j->kind == Kind::Batch && translator->retryable() && j->retries < retryDelays_.size()) {
        const int delay = retryDelays_[j->retries++];
        j->waiting = true;
        QTimer::singleShot(delay, this, [this, id] {
            if (Job* again = job(id)) { again->waiting = false; pump(); }
        });
    } else {
        fail(id, error, translator->requiresUserAction());
    }
    pump();
}
void TranslationQueue::finish(quint64 id, bool fromCache) {
    for (int i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].id != id) continue;
        const Job done = jobs_.takeAt(i);
        emit finished(id, done.results, done.detected, fromCache);
        return;
    }
}
void TranslationQueue::fail(quint64 id, const QString& error, bool userAction) {
    for (int i = 0; i < jobs_.size(); ++i) {
        if (jobs_[i].id != id) continue;
        jobs_.removeAt(i);
        emit failed(id, error, userAction);
        return;
    }
}
void TranslationQueue::cancel(quint64 id) {
    for (auto& worker : workers_)
        if (worker.job == id) { worker.translator->cancel(); worker.job = 0; worker.chunk = -1; }
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [id](const Job& j) { return j.id == id; }), jobs_.end());
    pump();
}
void TranslationQueue::cancelAll() {
    for (auto& worker : workers_)
        if (worker.job) { worker.translator->cancel(); worker.job = 0; worker.chunk = -1; }
    jobs_.clear();
}
