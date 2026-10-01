#pragma once
#include <QLocalServer>
#include <QObject>
#include <QStringList>

// One running app per Windows user and settings profile (QA R-12: a second copy could not register
// the global hotkeys and silently did nothing). A second launch hands its command line to the
// first one and exits; the first one decides what to show.
class SingleInstance final : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);
    ~SingleInstance() override;
    // true: this process is the primary instance and now listens for later launches.
    // false: a running instance received `arguments`; this process should exit.
    bool claim(const QStringList& arguments, int timeoutMs = 2000);
    bool listening() const { return server_.isListening(); }
    static QString serverName(const QString& key);
signals:
    void activated(QStringList arguments);
private:
    bool forward(const QStringList& arguments, int timeoutMs);
    void accept();
    QLocalServer server_;
    QString name_;
};
