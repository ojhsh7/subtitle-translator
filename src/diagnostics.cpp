#include "diagnostics.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <atomic>

Q_LOGGING_CATEGORY(stApp, "st.app")
Q_LOGGING_CATEGORY(stPerf, "st.perf")

namespace {
struct State {
    QMutex mutex;
    QFile file;
    Diagnostics::Options options;
    QtMessageHandler previous = nullptr;
    bool active = false;
};
State& state() { static State s; return s; }
std::atomic_bool logText{false};
const QElapsedTimer& sharedClock() {
    static const QElapsedTimer timer = [] { QElapsedTimer t; t.start(); return t; }();
    return timer;
}
const char* level(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warn";
    case QtCriticalMsg: return "error";
    case QtFatalMsg: return "fatal";
    }
    return "info";
}
void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    State& s = state();
    static thread_local bool writing = false; // a warning raised while writing must not deadlock
    if (!writing) {
        writing = true;
        QMutexLocker lock(&s.mutex);
        if (s.active && s.file.isOpen()) {
            QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
            line += QLatin1Char(' ') + QLatin1String(level(type)) + QLatin1Char(' ') +
                    QLatin1String(context.category ? context.category : "default") + QLatin1String(": ");
            line += QString(message).replace(QLatin1Char('\n'), QLatin1String("\\n"));
            line += QLatin1Char('\n');
            const QByteArray bytes = line.toUtf8();
            if (s.file.size() + bytes.size() > s.options.maxBytes) {
                const QString path = s.file.fileName();
                s.file.close();
                Diagnostics::rotate(path, s.options.files);
                s.file.setFileName(path);
                s.file.open(QIODevice::WriteOnly | QIODevice::Append);
            }
            s.file.write(bytes);
            s.file.flush();
        }
        lock.unlock();
        writing = false;
    }
    if (s.previous) s.previous(type, context, message);
}
}

namespace Diagnostics {
QString defaultDirectory() {
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString base = local.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) : local;
    return QDir(base).filePath(QStringLiteral("SubtitleTranslator/logs"));
}
void rotate(const QString& path, int files) {
    const QFileInfo info(path);
    const QString stem = info.dir().filePath(info.completeBaseName());
    const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    QFile::remove(stem + QLatin1Char('.') + QString::number(files - 1) + suffix);
    for (int i = files - 2; i >= 1; --i)
        QFile::rename(stem + QLatin1Char('.') + QString::number(i) + suffix,
                      stem + QLatin1Char('.') + QString::number(i + 1) + suffix);
    if (files > 1) QFile::rename(path, stem + QStringLiteral(".1") + suffix);
    else QFile::remove(path);
}
bool init(const Options& options) {
    State& s = state();
    QMutexLocker lock(&s.mutex);
    if (s.active) return true;
    if (options.directory.isEmpty() || !QDir().mkpath(options.directory)) return false;
    s.options = options;
    if (s.options.files < 1) s.options.files = 1;
    if (s.options.maxBytes < 4096) s.options.maxBytes = 4096;
    s.file.setFileName(QDir(options.directory).filePath(QStringLiteral("app.log")));
    if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    logText = options.logText;
    s.active = true;
    s.previous = qInstallMessageHandler(handler);
    sharedClock(); // start the shared clock now
    return true;
}
void shutdown() {
    State& s = state();
    QMutexLocker lock(&s.mutex);
    if (!s.active) return;
    qInstallMessageHandler(s.previous);
    s.previous = nullptr;
    s.active = false;
    s.file.close();
}
bool active() { QMutexLocker lock(&state().mutex); return state().active; }
QString currentFile() { QMutexLocker lock(&state().mutex); return state().file.fileName(); }
void setLogText(bool enabled) { logText = enabled; }
QString sensitive(const QString& text) {
    return logText ? text : QStringLiteral("<%1 chars>").arg(text.size());
}
qint64 nowMs() { return sharedClock().elapsed(); }
void stage(quint64 id, const char* mode, const char* name, qint64 ms) {
    qCInfo(stPerf, "id=%llu mode=%s stage=%s ms=%lld", static_cast<unsigned long long>(id), mode, name,
           static_cast<long long>(ms));
}
}

UiStallMonitor::UiStallMonitor(QObject* parent, int intervalMs, int thresholdMs)
    : QObject(parent), interval_(intervalMs), threshold_(thresholdMs) {
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(interval_);
    connect(&timer_, &QTimer::timeout, this, &UiStallMonitor::tick);
}
void UiStallMonitor::start() { reset(); clock_.start(); last_ = 0; timer_.start(); }
void UiStallMonitor::stop() { timer_.stop(); }
void UiStallMonitor::reset() { stalls_ = 0; worst_ = 0; samples_ = 0; last_ = clock_.isValid() ? clock_.elapsed() : -1; }
void UiStallMonitor::tick() {
    const qint64 now = clock_.elapsed();
    if (last_ >= 0) {
        const qint64 late = now - last_ - interval_;
        ++samples_;
        if (late > worst_) worst_ = late;
        if (late > threshold_) {
            ++stalls_;
            qCWarning(stPerf, "ui-stall late_ms=%lld", static_cast<long long>(late));
        }
    }
    last_ = now;
}
