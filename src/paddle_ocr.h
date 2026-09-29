#pragma once
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include <map>
#include <mutex>
#include <vector>

// Windows adapter: PaddleOCR PP-OCRv5 text detection and line recognition through ONNX Runtime.
// The detection model finds the text lines inside the selected area (a dialog box also holds a
// name tag, ruled lines, frames and scenery); the recognition model reads each line.
class PaddleRecognizer {
public:
    struct Line { QString text; double confidence = 0; }; // confidence 0..1
    PaddleRecognizer();
    ~PaddleRecognizer();
    bool load(const QString& modelPath, QString& error);
    bool isLoaded() const { return model_ != nullptr; }
    Line recognize(const QImage& line) const;
    // Model input, as RapidOCR prepares it: shorter side raised to 30 px (rounded to 32),
    // then height 48, width at least 320, BGR scaled to [-1, 1], zero padded on the right.
    static std::vector<float> inputTensor(const QImage& line, int& width);
    // CTC greedy decoding: best class per step, repeats and blanks (index 0) removed.
    static Line decode(const float* probabilities, int steps, int classes, const QStringList& dictionary);
private:
    struct Model;
    std::unique_ptr<Model> model_;
};

// DB text detection (PP-OCRv5 mobile det), with RapidOCR's post-processing for horizontal text.
class PaddleDetector {
public:
    PaddleDetector();
    ~PaddleDetector();
    bool load(const QString& modelPath, QString& error);
    // One box per text line in region coordinates, top to bottom; boxes on one line are joined.
    QList<QRect> lines(const QImage& region) const;
    // Model input: the region enlarged 1.5x (shorter side at least 64, at most 2 million pixels,
    // longest side 2560), sides rounded to multiples of 32, BGR scaled to [-1, 1].
    static std::vector<float> inputTensor(const QImage& region, int& width, int& height);
    // Probability map -> text boxes: threshold 0.3, 2x2 dilation, connected regions with mean
    // score of at least 0.5, expanded by area * 1.6 / perimeter, scaled to the original size.
    static QList<QRect> boxes(const float* probabilities, int width, int height, const QSize& original);
    // Groups boxes that share a text line and orders lines top to bottom.
    static QList<QRect> joinLines(QList<QRect> boxes);
private:
    struct Model;
    std::unique_ptr<Model> model_;
};

// Fallback line finder when the detection model is missing or finds nothing: row bands dense in
// strong edges, padded. Returns the whole region when no band is found.
QList<QImage> textLines(const QImage& region, bool tallMarks = false);
// The Arabic recognition model emits visual order. Restore Unicode logical order using Windows ICU.
QString arabicLogicalOrder(const QString& visual);

// Chooses models from the app's OCR language code and reads a region line by line.
class PaddleOcr {
public:
    static constexpr const char* KoreanModel = "korean_PP-OCRv5_rec_mobile.onnx";
    static constexpr const char* CjkModel = "ch_PP-OCRv5_rec_mobile.onnx"; // Chinese, Japanese, English
    static constexpr const char* LatinModel = "latin_PP-OCRv5_rec_mobile.onnx";
    static constexpr const char* CyrillicModel = "eslav_PP-OCRv5_rec_mobile.onnx";
    static constexpr const char* ThaiModel = "th_PP-OCRv5_rec_mobile.onnx";
    static constexpr const char* ArabicModel = "arabic_PP-OCRv5_rec_mobile.onnx";
    static constexpr const char* HindiModel = "devanagari_PP-OCRv5_rec_mobile.onnx";
    static constexpr const char* DetectionModel = "ch_PP-OCRv5_det_mobile.onnx"; // all scripts
    // detected: the lines came from the detection model (not the fallback line finder).
    struct Result { QString text; double confidence = 0; int modelRuns = 0; bool detected = false; };
    // Multi-language auto-detect runs the remaining five models when the first reading scores below this.
    static constexpr double StageTwoBelow = 0.85;
    explicit PaddleOcr(QString modelDir) : dir_(std::move(modelDir)) {}
    // Loads the needed models on first use. Safe to call from a worker thread.
    Result read(const QImage& region, const QString& languages, QString& error);
    static QStringList modelFiles(const QString& languages);
    static bool needsKorean(const QString& languages) { return modelFiles(languages).contains(QLatin1String(KoreanModel)); }
    static bool needsCjk(const QString& languages) { return modelFiles(languages).contains(QLatin1String(CjkModel)); }
    static bool available(const QString& modelDir, const QString& languages);
private:
    QString dir_;
    std::mutex mutex_;
    std::map<QString, std::unique_ptr<PaddleRecognizer>> models_;
    std::unique_ptr<PaddleDetector> detector_;
    bool detectorTried_ = false;
};
