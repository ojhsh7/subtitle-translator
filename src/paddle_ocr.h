#pragma once
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <vector>

// Windows adapter: PaddleOCR PP-OCRv5 text detection and line recognition through ONNX Runtime.
// The detection model finds the text lines inside the selected area (a dialog box also holds a
// name tag, ruled lines, frames and scenery); the recognition model reads each line.
class PaddleRecognizer {
public:
    // confidence 0..1: the mean probability of the emitted dictionary entries, kept in order in `pieces`.
    struct Piece { QString text; float probability = 0; };
    struct Line { QString text; double confidence = 0; QList<Piece> pieces; };
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
    // One recognized line as chosen among the models, kept for diagnostics and filtering.
    // others: what the models that were not chosen read on the same line, "model: text".
    struct LineInfo { QString text; double confidence = 0; double lowestCharacter = 0; QString model;
                      QList<PaddleRecognizer::Piece> pieces; QStringList others; };
    // detected: the lines came from the detection model (not the fallback line finder).
    // detectorBoxes: text lines the detection model found; -1 when the model is not installed.
    // dropped: lines removed or shortened by filterLine, as read before filtering.
    struct Result {
        QString text; double confidence = 0; int modelRuns = 0; bool detected = false;
        int detectorBoxes = -1; QList<LineInfo> lines; QStringList dropped;
    };
    // False-text filters. Game symbols missing from the model dictionaries come out as look-alike
    // letters ("○" -> "O", "♪ Hello ♪" -> "s Hello s"). Thresholds from measurements in
    // docs/plan/2026-10-25-ocr-false-text.ko.md: on such lines the surest letter scored 0.14-0.78,
    // on real short lines (はい, いいえ, OK, 네, 응, 아!) at least 0.917.
    static constexpr int ShortLineLetters = 3;     // a line with at most this many letters or digits
    static constexpr double ShortLineSurest = 0.85; // needs one letter at least this sure
    // A lone Latin/Hangul/kana letter or digit at either end of a longer line, separated by a space,
    // is dropped below this (misread symbols: 0.16-0.45; real lone letters such as "Я": 0.6 or more).
    static constexpr double EdgeLetterBelow = 0.5;
    // Cross-model repairs when the Korean and the Chinese/Japanese/English models both read a line
    // (measurements in docs/plan/2026-10-25-cleanup-and-reliability.ko.md):
    // P-19: the Korean model reads "0" before a divider as "이" with 0.99 ("Lv.1이|전사"); keep "0" only
    // when the other model read exactly those digits plus "0". A space or a letter after "이" is left
    // alone: "레벨 10이 되었습니다" is real Korean, and the other model reads it as "100合".
    static QString restoreZeroBeforeSymbol(const QString& korean, const QString& other);
    // P-20: an "I" glued between capitals ("HPIMPISP") where the other model read "|" in the same
    // place is a divider. Real capital words (MISSION, QUIT) read as "I" in both models.
    static QString preferDividerOverI(const QString& chosen, const QString& other);
    struct Filtered { bool drop = false; bool changed = false; QList<PaddleRecognizer::Piece> kept; };
    // visualOrder: the pieces are not in reading order (Arabic), so edge letters are left alone.
    static Filtered filterLine(const QList<PaddleRecognizer::Piece>& pieces, bool visualOrder = false);
    // Multi-language auto-detect runs the remaining five models when the first reading scores below this.
    static constexpr double StageTwoBelow = 0.85;
    explicit PaddleOcr(QString modelDir) : dir_(std::move(modelDir)) {}
    // Loads the needed models on first use. Safe to call from a worker thread.
    // cancelled: checked before every line and model run, so a cancelled read releases the engine
    // after at most one recognition instead of finishing the whole region (R-02, P-03).
    Result read(const QImage& region, const QString& languages, QString& error,
                const std::atomic<bool>* cancelled = nullptr);
    // Text lines the detection model finds, for engines that only recognize (Tesseract);
    // -1 when the detection model is not installed. Safe to call from a worker thread.
    int countTextLines(const QImage& region);
    static QStringList modelFiles(const QString& languages);
    static bool needsKorean(const QString& languages) { return modelFiles(languages).contains(QLatin1String(KoreanModel)); }
    static bool needsCjk(const QString& languages) { return modelFiles(languages).contains(QLatin1String(CjkModel)); }
    static bool available(const QString& modelDir, const QString& languages);
private:
    void loadDetector(); // once; caller holds mutex_
    QString dir_;
    std::mutex mutex_;
    std::map<QString, std::unique_ptr<PaddleRecognizer>> models_;
    std::unique_ptr<PaddleDetector> detector_;
    bool detectorTried_ = false;
};
