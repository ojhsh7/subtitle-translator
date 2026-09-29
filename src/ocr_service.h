#pragma once
#include <QObject>
#include <QImage>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
class OcrService final : public QObject {
    Q_OBJECT
public:
    explicit OcrService(QObject* parent = nullptr);
    ~OcrService() override;
    void recognize(const QImage&, const QString& executable, const QString& tessdataDir, int psm = 6);
    void probe(const QString& executable, const QString& tessdataDir);
    void cancel();
    bool isBusy() const { return busy_; }
    qint64 elapsedMs() const { return elapsedMs_; }
    quint64 peakWorkingSetBytes() const { return peakMemory_; }
signals:
    void recognized(QString text);
    void probeFinished(bool ok, QString message);
    void failed(QString message);
private:
    void launch(const QString& executable, const QStringList& args, QByteArray bytes);
    void fail(const QString& message);
    void collect();
    void sample();
    void metrics();
    QProcess* process_ = nullptr;
    QTimer deadline_, memoryTimer_;
    QElapsedTimer elapsed_;
    QByteArray output_, errors_;
    bool busy_ = false, probing_ = false;
    quint64 generation_ = 0, peakMemory_ = 0;
    qint64 elapsedMs_ = 0;
    void* processHandle_ = nullptr;
};
