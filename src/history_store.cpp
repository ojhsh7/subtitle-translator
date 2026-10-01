#include "history_store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

HistoryStore::HistoryStore(QObject* parent) : QObject(parent) {}
QString HistoryStore::defaultPath() {
    // %LOCALAPPDATA%\PersonalTools\SubtitleTranslator: follows the settings profile, so test and QA
    // runs never read or delete the user's own history.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("history.jsonl"));
}
quint64 HistoryStore::add(const QString& mode, const QString& source, const QString& translation, qint64 startMs) {
    HistoryEntry entry;
    entry.id = ++nextId_;
    entry.when = QDateTime::currentDateTime();
    entry.startMs = startMs;
    entry.mode = mode;
    entry.source = source;
    entry.translation = translation;
    entries_ << entry;
    const bool trimmed = entries_.size() > Limit;
    while (entries_.size() > Limit) entries_.removeFirst();
    if (persistent_) { if (trimmed) rewriteFile(); else appendToFile(entry); }
    emit added(entry);
    emit changed();
    return entry.id;
}
void HistoryStore::setEnd(quint64 id, qint64 endMs) {
    for (auto& entry : entries_)
        if (entry.id == id && entry.endMs < 0) { entry.endMs = endMs; emit changed(); return; }
}
void HistoryStore::setTranslation(quint64 id, const QString& translation) {
    for (auto& entry : entries_)
        if (entry.id == id) {
            entry.translation = translation;
            if (persistent_) rewriteFile();
            emit changed();
            return;
        }
}
void HistoryStore::clear() {
    entries_.clear();
    if (persistent_) rewriteFile();
    emit changed();
}
void HistoryStore::setPersistent(bool keep, const QString& path) {
    path_ = path;
    if (!keep) {
        persistent_ = false;
        QFile::remove(path_); // "앱 종료 시 삭제" means nothing stays on disk
        return;
    }
    if (persistent_) return;
    persistent_ = true;
    QList<HistoryEntry> current = entries_;
    entries_.clear();
    load();
    for (auto& entry : current) { entry.id = ++nextId_; entries_ << entry; }
    while (entries_.size() > Limit) entries_.removeFirst();
    rewriteFile();
    emit changed();
}
namespace {
QJsonObject toJson(const HistoryEntry& e) {
    return {{"when", e.when.toString(Qt::ISODateWithMs)}, {"mode", e.mode}, {"source", e.source},
            {"translation", e.translation}};
}
}
void HistoryStore::appendToFile(const HistoryEntry& entry) {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QFile file(path_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(QJsonDocument(toJson(entry)).toJson(QJsonDocument::Compact) + '\n');
}
void HistoryStore::rewriteFile() {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return;
    for (const auto& entry : entries_) file.write(QJsonDocument(toJson(entry)).toJson(QJsonDocument::Compact) + '\n');
    file.commit();
}
void HistoryStore::load() {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) return;
    while (!file.atEnd()) {
        const QJsonObject object = QJsonDocument::fromJson(file.readLine()).object();
        if (object.isEmpty()) continue;
        HistoryEntry entry;
        entry.id = ++nextId_;
        entry.when = QDateTime::fromString(object.value("when").toString(), Qt::ISODateWithMs);
        entry.mode = object.value("mode").toString();
        entry.source = object.value("source").toString();
        entry.translation = object.value("translation").toString();
        entries_ << entry;
    }
    while (entries_.size() > Limit) entries_.removeFirst();
}
QString HistoryStore::toText(const QList<HistoryEntry>& entries, bool withSource) {
    QString out;
    for (const auto& e : entries) {
        out += e.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QLatin1Char('\n');
        if (withSource) out += e.source + QLatin1Char('\n');
        out += e.translation + QStringLiteral("\n\n");
    }
    return out;
}
QString HistoryStore::srtTime(qint64 ms) {
    ms = std::max<qint64>(0, ms);
    return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
QString HistoryStore::toSrt(const QList<HistoryEntry>& entries, bool withSource) {
    QList<HistoryEntry> list = entries;
    // Entries without a measured start (text or file modes) fall back to their clock time.
    const auto start = [](const HistoryEntry& e) { return e.startMs >= 0 ? e.startMs : e.when.toMSecsSinceEpoch(); };
    std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) { return start(a) < start(b); });
    QString out;
    if (list.isEmpty()) return out;
    const qint64 base = start(list.first());
    for (int i = 0; i < list.size(); ++i) {
        const qint64 begin = start(list[i]) - base;
        qint64 end = list[i].endMs >= 0 ? list[i].endMs - base : -1;
        if (end < 0) end = i + 1 < list.size() ? std::min(start(list[i + 1]) - base, begin + 4000) : begin + 4000;
        end = std::max(end, begin + 500);
        out += QString::number(i + 1) + QLatin1Char('\n') + srtTime(begin) + QStringLiteral(" --> ") + srtTime(end) + QLatin1Char('\n');
        if (withSource) out += list[i].source + QLatin1Char('\n');
        out += list[i].translation + QStringLiteral("\n\n");
    }
    return out;
}
