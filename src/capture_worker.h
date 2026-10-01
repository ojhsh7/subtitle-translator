#pragma once
#include "change_detector.h"
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QThread>
#include <atomic>

// Watches one screen rectangle on its own thread (QA R-15). The GUI thread only asks for a check
// and receives the frames that must be read, so capturing, shrinking and comparing pixels never
// freeze the window. The change detector is shared under a lock so the GUI thread can still
// schedule an OCR retry or forget the last line synchronously.
class CaptureWorker final : public QObject {
    Q_OBJECT
public:
    explicit CaptureWorker(QObject* parent = nullptr);
    ~CaptureWorker() override;
    void setProfile(const ChangeDetector::Profile& profile);
    // Physical pixels of the virtual desktop; dpr is stored on the frames like a Qt screen grab.
    // Returns false when a check is still running: the caller simply tries again on its next tick.
    bool requestCheck(const QRect& physical, qreal dpr, qint64 nowMs, quint64 ticket);
    bool checking() const { return pending_.load(); }
    void reset();
    bool retryAfterFailure(qint64 nowMs);
    // Time of the last capture on the worker, in Diagnostics::nowMs() milliseconds.
    qint64 lastCaptureMs() const { return lastCaptureMs_.load(); }
    // Fraction of pure black pixels in the last captured frame (0..1), for protected-video hints.
    double lastBlackFraction() const { return lastBlack_.load(); }
signals:
    // A settled new picture to read. `ticket` is echoed so callers can drop stale results.
    void frameReady(QImage frame, qint64 capturedAtMs, quint64 ticket);
    void blank(qint64 capturedAtMs, quint64 ticket);
    void checked(quint64 ticket); // every finished check, also when nothing changed
    void captureFailed(QString error, quint64 ticket);
private:
    void check(QRect physical, qreal dpr, qint64 nowMs, quint64 ticket);
    QThread thread_;
    QObject* runner_ = nullptr; // lives on thread_
    mutable QMutex mutex_;
    ChangeDetector detector_;
    std::atomic_bool pending_{false};
    std::atomic<qint64> lastCaptureMs_{-1};
    std::atomic<double> lastBlack_{0.0};
};
