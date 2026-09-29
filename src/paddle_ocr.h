#pragma once
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include <map>
#include <mutex>
#include <vector>

// Windows adapter: PaddleOCR PP-OCRv5 text-line recognition through ONNX Runtime.
// Only the recognition model runs. The user already selected the subtitle area, and
// textLines() cuts it into single lines, so no detection model is needed.
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

// Cuts a selected region into text-line images: row bands dense in strong edges, padded.
// Returns the whole region when no band is found.
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
    struct Result { QString text; double confidence = 0; int modelRuns = 0; };
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
};
