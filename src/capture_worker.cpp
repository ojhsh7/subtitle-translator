#include "capture_worker.h"
#include "capture_service.h"
#include "diagnostics.h"
#include <QMetaObject>

CaptureWorker::CaptureWorker(QObject* parent) : QObject(parent) {
    runner_ = new QObject;
    runner_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, runner_, &QObject::deleteLater);
    thread_.setObjectName(QStringLiteral("capture"));
    thread_.start(QThread::LowPriority);
}
CaptureWorker::~CaptureWorker() {
    thread_.quit();
    thread_.wait();
}
void CaptureWorker::setProfile(const ChangeDetector::Profile& profile) {
    QMutexLocker lock(&mutex_);
    detector_.setProfile(profile);
}
bool CaptureWorker::requestCheck(const QRect& physical, qreal dpr, qint64 nowMs, quint64 ticket) {
    bool idle = false;
    if (!pending_.compare_exchange_strong(idle, true)) return false;
    QMetaObject::invokeMethod(runner_, [this, physical, dpr, nowMs, ticket] { check(physical, dpr, nowMs, ticket); },
                              Qt::QueuedConnection);
    return true;
}
void CaptureWorker::reset() {
    QMutexLocker lock(&mutex_);
    detector_.reset();
}
bool CaptureWorker::retryAfterFailure(qint64 nowMs) {
    QMutexLocker lock(&mutex_);
    return detector_.retryAfterFailure(nowMs);
}
void CaptureWorker::check(QRect physical, qreal dpr, qint64 nowMs, quint64 ticket) {
    QString error;
    const qint64 started = Diagnostics::nowMs();
    const QImage image = Capture::physical(physical, dpr, error);
    lastCaptureMs_ = started;
    if (image.isNull()) {
        pending_ = false;
        emit captureFailed(error, ticket);
        return;
    }
    // Cheap protected-video probe: DRM-protected players are captured as pure black.
    {
        const QImage small = image.scaled(64, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        int black = 0;
        for (int y = 0; y < small.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(small.constScanLine(y));
            for (int x = 0; x < small.width(); ++x) black += (row[x] & 0x00ffffffu) == 0;
        }
        lastBlack_ = double(black) / (small.width() * small.height());
    }
    ChangeDetector::Decision decision;
    {
        QMutexLocker lock(&mutex_);
        decision = detector_.feed(image, nowMs);
    }
    const qint64 spent = Diagnostics::nowMs() - started;
    if (spent > 40) qCInfo(stPerf, "capture-check slow ms=%lld size=%dx%d", static_cast<long long>(spent),
                           physical.width(), physical.height());
    pending_ = false;
    if (decision == ChangeDetector::Decision::Process) emit frameReady(image, started, ticket);
    else if (decision == ChangeDetector::Decision::Blank) emit blank(started, ticket);
    emit checked(ticket);
}
