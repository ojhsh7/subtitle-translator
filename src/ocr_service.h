#pragma once
#include <QObject>
#include <QImage>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include "paddle_ocr.h"
class OcrService final : public QObject {
    Q_OBJECT
public:
    explicit OcrService(QObject* parent = nullptr);
    ~OcrService() override;
    // languages: a Tesseract code from Languages::Ocr, e.g. "jpn+eng+kor".
    // With PaddleOCR models set (setPaddleModelDir) they read the image first: measured far more
    // accurate on game text. Tesseract is the fallback when Paddle finds nothing or is missing;
    // it reads the image as it is, then light-text masks, keeping the most confident reading.
    void recognize(const QImage&, const QString& executable, const QString& tessdataDir, int psm = 0,
                   const QString& languages = QStringLiteral("jpn+eng+kor"));
    // Folder with the PP-OCRv5 recognition models; empty disables PaddleOCR.
    void setPaddleModelDir(const QString& dir);
    QString engineUsed() const { return engineUsed_; } // "PaddleOCR" or "Tesseract" for the last result
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
    // Tesseract often reads a standalone English "I" as "|". PaddleOCR does not, and game UIs use "|"
    // as a divider ("HP | MP"), so that repair applies to Tesseract readings only.
    static QString cleanText(const QString& text, bool fromTesseract = true);
    // Conservative blank check: every pixel must match, with no resizing or contrast threshold.
    static bool isUniformImage(const QImage& image);
    // Tesseract reading that is treated as no text. On empty, bordered or noisy areas it invented
    // strings at confidence 13.1, 27.9, 33.0 and 50.4, but it also read a real "응" correctly at 36.1,
    // so only readings clearly below every correct one are dropped (measurements in
    // docs/plan/2026-10-25-ocr-false-text.ko.md). Real short lines are kept (DECISIONS 2026-10-01).
    static constexpr double UnsureBelow = 30;
    static bool isUnsureReading(const Reading& reading) { return reading.confidence < UnsureBelow; }
    void cancel();
    bool isBusy() const { return busy_; }
    qint64 elapsedMs() const { return elapsedMs_; }
    quint64 peakWorkingSetBytes() const { return peakMemory_; }
    int attemptsUsed() const { return attemptsUsed_; }
    // Diagnostics of the last read: PaddleOCR's lines (empty when it did not run) and the
    // confidence (0..100) of the Tesseract reading that was used (-1 when Tesseract did not read).
    const PaddleOcr::Result& paddleDetails() const { return paddleDetails_; }
    double tesseractConfidence() const { return tesseractConfidence_; }
signals:
    void recognized(QString text);
    void probeFinished(bool ok, QString message);
    void failed(QString message);
private:
    void launch(const QString& executable, const QStringList& args, QByteArray bytes);
    void runPaddle(const QImage& image, quint64 ticket);
    void runTesseract(const QImage& image, quint64 ticket);
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
    QString languages_, executable_, engineUsed_, paddleDir_;
    std::shared_ptr<PaddleOcr> paddle_; // shared with the worker thread, which may outlive a cancel
    QStringList arguments_;
    QList<QByteArray> attempts_; // PNG candidates: original, light mask, thickened mask, bright mask
    int attempt_ = 0, attemptsUsed_ = 0;
    Reading best_;
    bool bestFromMask_ = false;
    PaddleOcr::Result paddleDetails_;
    double tesseractConfidence_ = -1;
    quint64 generation_ = 0, peakMemory_ = 0;
    qint64 elapsedMs_ = 0;
    void* processHandle_ = nullptr;
};
