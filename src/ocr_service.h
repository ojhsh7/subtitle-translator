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
    // Reads the image as it is first; when Tesseract is unsure it also tries light-text masks
    // (white game text with outlines or shadows) and keeps the most confident reading.
    void recognize(const QImage&, const QString& executable, const QString& tessdataDir, int psm = 0,
                   const QString& languages = QStringLiteral("jpn+eng+kor"));
    void probe(const QString& executable, const QString& tessdataDir,
               const QString& languages = QStringLiteral("jpn+eng+kor"));
    // psm 0 = automatic: a short strip is read as one line (PSM 7), anything taller as a block (PSM 6).
    static int resolvePsm(int psm, const QSize& imageSize);
    static constexpr int SingleLineMaxHeight = 64;
    // Bright, unsaturated pixels (white game text) as black ink on white, upscaled 3x.
    // anyColour: brightness only, for text drawn with ClearType-style colour fringes.
    static QImage lightTextMask(const QImage& image, double* coverage = nullptr, bool anyColour = false);
    // Thickens black ink by one pixel so thin strokes of small outlined text survive.
    static QImage growInk(const QImage& mask);
    struct Reading { QString text; double confidence = -1; };
    // Parses one run's combined output: TSV rows give per-word confidence (averaged per character),
    // the plain-text lines give the sentence with Tesseract's own spacing.
    static Reading parseOutput(const QString& output);
    static constexpr double ConfidentEnough = 90; // stop trying other images
    static constexpr double MaskMargin = 5;       // a mask must beat the original clearly
    // Tesseract often reads a standalone English "I" as "|".
    static QString cleanText(const QString& text);
    void cancel();
    bool isBusy() const { return busy_; }
    qint64 elapsedMs() const { return elapsedMs_; }
    quint64 peakWorkingSetBytes() const { return peakMemory_; }
    int attemptsUsed() const { return attemptsUsed_; }
signals:
    void recognized(QString text);
    void probeFinished(bool ok, QString message);
    void failed(QString message);
private:
    void launch(const QString& executable, const QStringList& args, QByteArray bytes);
    void runAttempt();
    void attemptFinished(const QString& tsv);
    void releaseProcess();
    void fail(const QString& message);
    void collect();
    void sample();
    void metrics();
    QProcess* process_ = nullptr;
    QTimer deadline_, memoryTimer_;
    QElapsedTimer elapsed_;
    QByteArray output_, errors_;
    bool busy_ = false, probing_ = false;
    QString languages_, executable_;
    QStringList arguments_;
    QList<QByteArray> attempts_; // PNG candidates: original, light mask, thickened mask, bright mask
    int attempt_ = 0, attemptsUsed_ = 0;
    Reading best_;
    bool bestFromMask_ = false;
    quint64 generation_ = 0, peakMemory_ = 0;
    qint64 elapsedMs_ = 0;
    void* processHandle_ = nullptr;
};
