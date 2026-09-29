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
    // languages: a Tesseract code from Languages::Ocr, e.g. "jpn+eng+kor".
    void recognize(const QImage&, const QString& executable, const QString& tessdataDir, int psm = 0,
                   const QString& languages = QStringLiteral("jpn+eng+kor"));
    void probe(const QString& executable, const QString& tessdataDir,
               const QString& languages = QStringLiteral("jpn+eng+kor"));
    // Upscales small subtitle lines; Tesseract reads text best at roughly 30 px or taller.
    static QImage prepare(const QImage& image);
    // psm 0 = automatic: a short strip is read as one line (PSM 7), anything taller as a block (PSM 6).
    static int resolvePsm(int psm, const QSize& imageSize);
    static constexpr int SingleLineMaxHeight = 64;
    // Tesseract often reads a standalone English "I" as "|".
    static QString cleanText(const QString& text);
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
    QString languages_;
    quint64 generation_ = 0, peakMemory_ = 0;
    qint64 elapsedMs_ = 0;
    void* processHandle_ = nullptr;
};
