#pragma once
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(stApp)
Q_DECLARE_LOGGING_CATEGORY(stPerf)

// Windows app diagnostics: a rotating log file (default 5 MB x 3) and per-request stage timings.
// The log never contains OCR text, translations, API keys or file paths; callers pass such text
// only through sensitive(), which keeps it out unless the user turned text logging on.
namespace Diagnostics {
struct Options {
    QString directory;                  // e.g. %LOCALAPPDATA%\SubtitleTranslator\logs
    qint64 maxBytes = 5 * 1024 * 1024;  // per file
    int files = 3;                      // current file + rotated copies
    bool logText = false;               // user setting "진단 로그에 원문 남기기"
};
QString defaultDirectory();
bool init(const Options& options); // installs the Qt message handler; safe to call once per process
void shutdown();
bool active();
QString currentFile();
void setLogText(bool enabled);
// Returns the text itself when text logging is on, otherwise only its length.
QString sensitive(const QString& text);
// Milliseconds on one monotonic clock shared by every thread (capture worker, OCR, GUI).
qint64 nowMs();
// One pipeline step for later latency analysis, e.g. "id=12 mode=video stage=ocr ms=84".
void stage(quint64 id, const char* mode, const char* stage, qint64 ms);
// Rotation helper, exposed for tests: renames base.log -> base.1.log ... keeping `files` in total.
void rotate(const QString& path, int files);
}

// Measures how late the GUI thread runs a short timer. A gap longer than the threshold means the
// window could not repaint or react to input for that long. Enabled only for measurements
// (--perf, tests); it is not running in normal use, so it costs no idle CPU.
class UiStallMonitor final : public QObject {
public:
    explicit UiStallMonitor(QObject* parent = nullptr, int intervalMs = 16, int thresholdMs = 50);
    void start();
    void stop();
    void reset();
    bool isActive() const { return timer_.isActive(); }
    int stalls() const { return stalls_; }
    qint64 worstMs() const { return worst_; }
    qint64 samples() const { return samples_; }
private:
    void tick();
    QTimer timer_;
    QElapsedTimer clock_;
    qint64 last_ = -1;
    int interval_, threshold_;
    int stalls_ = 0;
    qint64 worst_ = 0;
    qint64 samples_ = 0;
};
