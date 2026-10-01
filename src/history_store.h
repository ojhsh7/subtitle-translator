#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

// Recent translations of every mode: kept in memory and deleted when the app exits (default),
// or kept in %LOCALAPPDATA%\SubtitleTranslator\history.jsonl when the user chooses to.
struct HistoryEntry {
    quint64 id = 0;
    QDateTime when;
    qint64 startMs = -1; // Diagnostics::nowMs() when the line appeared (video SRT timing)
    qint64 endMs = -1;   // when it disappeared or was replaced; -1 while unknown
    QString mode;        // "game", "video", "text", "file"
    QString source;
    QString translation;
};
class HistoryStore final : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(QObject* parent = nullptr);
    static constexpr int Limit = 5000;
    quint64 add(const QString& mode, const QString& source, const QString& translation, qint64 startMs = -1);
    void setEnd(quint64 id, qint64 endMs);
    void setTranslation(quint64 id, const QString& translation);
    const QList<HistoryEntry>& entries() const { return entries_; }
    void clear();
    // Keeping history on disk: false (default) also deletes an earlier file.
    void setPersistent(bool keep, const QString& path = defaultPath());
    bool persistent() const { return persistent_; }
    static QString defaultPath();
    // Exports. SRT times are relative to the first exported entry; an unknown end becomes the next
    // start (at most 4 s later), never earlier than 0.5 s after the start.
    static QString toText(const QList<HistoryEntry>& entries, bool withSource = true);
    static QString toSrt(const QList<HistoryEntry>& entries, bool withSource = false);
    static QString srtTime(qint64 ms);
signals:
    void added(HistoryEntry entry);
    void changed();
private:
    void appendToFile(const HistoryEntry& entry);
    void rewriteFile();
    void load();
    QList<HistoryEntry> entries_;
    quint64 nextId_ = 0;
    bool persistent_ = false;
    QString path_;
};
