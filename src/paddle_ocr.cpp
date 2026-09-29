#include "paddle_ocr.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <icu.h> // Windows SDK; kept outside the portable core
#include "languages.h"

namespace {
Ort::Env& environment() {
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "subtitle-translator"); // one per process
    return env;
}
Ort::SessionOptions sessionOptions() {
    Ort::SessionOptions options;
    // Each cached model owns a thread pool. Bound it and stop idle spinning while gaming.
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    options.AddConfigEntry("session.inter_op.allow_spinning", "0");
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    return options;
}
// Pillow's greyscale conversion (ITU-R 601-2), so line finding matches the measured benchmark.
int luma(QRgb p) { return (qRed(p) * 19595 + qGreen(p) * 38470 + qBlue(p) * 7471 + 0x8000) >> 16; }
// Writes a BGR image scaled to [-1, 1] into a planar (CHW) tensor with the given row width.
void writePlanes(const QImage& scaled, int width, int height, std::vector<float>& data) {
    const size_t plane = size_t(height) * width;
    for (int y = 0; y < scaled.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
        for (int x = 0; x < scaled.width(); ++x) {
            const size_t at = size_t(y) * width + x;
            data[at] = qBlue(row[x]) / 127.5f - 1.0f; // BGR, as the models were trained with OpenCV images
            data[plane + at] = qGreen(row[x]) / 127.5f - 1.0f;
            data[2 * plane + at] = qRed(row[x]) / 127.5f - 1.0f;
        }
    }
}
}

struct PaddleRecognizer::Model {
    Ort::Session session{nullptr};
    QStringList dictionary; // index 0 = blank, last = space
    std::string input, output;
    bool arabic = false;
};
PaddleRecognizer::PaddleRecognizer() = default;
PaddleRecognizer::~PaddleRecognizer() = default;

bool PaddleRecognizer::load(const QString& modelPath, QString& error) {
    try {
        auto model = std::make_unique<Model>();
        model->session = Ort::Session(environment(), modelPath.toStdWString().c_str(), sessionOptions());
        Ort::AllocatorWithDefaultOptions allocator;
        model->input = model->session.GetInputNameAllocated(0, allocator).get();
        model->output = model->session.GetOutputNameAllocated(0, allocator).get();
        // The character list travels inside the model file as metadata.
        const auto characters = model->session.GetModelMetadata().LookupCustomMetadataMapAllocated("character", allocator);
        if (!characters) { error = QStringLiteral("OCR 모델에 문자 목록이 없습니다: %1").arg(QFileInfo(modelPath).fileName()); return false; }
        QStringList dictionary = QString::fromUtf8(characters.get()).split('\n');
        for (QString& entry : dictionary) if (entry.endsWith('\r')) entry.chop(1);
        // Like Python's splitlines(): a final newline (the Korean model has one) adds no entry.
        if (!dictionary.isEmpty() && dictionary.last().isEmpty()) dictionary.removeLast();
        dictionary.prepend(QStringLiteral("blank"));
        dictionary.append(QStringLiteral(" "));
        const auto shape = model->session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3 || (shape[2] > 0 && shape[2] != dictionary.size())) {
            error = QStringLiteral("OCR 모델의 문자 수가 목록과 다릅니다: %1").arg(QFileInfo(modelPath).fileName()); return false;
        }
        model->arabic = QFileInfo(modelPath).fileName() == QLatin1String(PaddleOcr::ArabicModel);
        model->dictionary = std::move(dictionary);
        model_ = std::move(model);
        return true;
    } catch (const Ort::Exception& e) {
        error = QStringLiteral("OCR 모델을 불러오지 못했습니다 (%1): %2").arg(QFileInfo(modelPath).fileName(), QString::fromUtf8(e.what()));
        return false;
    }
}

std::vector<float> PaddleRecognizer::inputTensor(const QImage& source, int& width) {
    QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int shorter = std::min(image.width(), image.height());
    if (shorter > 0 && shorter < 30) {
        const double ratio = 30.0 / shorter;
        // nearbyint rounds halves to even, like Python's round() in RapidOCR.
        const int h = std::max(32, int(std::nearbyint(int(image.height() * ratio) / 32.0)) * 32);
        const int w = std::max(32, int(std::nearbyint(int(image.width() * ratio) / 32.0)) * 32);
        image = image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    constexpr int Height = 48;
    const double ratio = double(image.width()) / std::max(1, image.height());
    width = int(Height * std::max(320.0 / Height, ratio));
    const int resized = std::min(width, int(std::ceil(Height * ratio)));
    const QImage scaled = image.scaled(std::max(1, resized), Height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB32);
    std::vector<float> data(size_t(3) * Height * width, 0.0f);
    writePlanes(scaled, width, Height, data);
    return data;
}

PaddleRecognizer::Line PaddleRecognizer::decode(const float* probabilities, int steps, int classes, const QStringList& dictionary) {
    Line line;
    double sum = 0;
    int kept = 0, previous = -1;
    for (int t = 0; t < steps; ++t) {
        const float* row = probabilities + size_t(t) * classes;
        const int best = int(std::max_element(row, row + classes) - row);
        if (best != 0 && best != previous && best < dictionary.size()) {
            line.text += dictionary[best];
            sum += row[best]; ++kept;
        }
        previous = best;
    }
    line.confidence = kept ? sum / kept : 0;
    return line;
}

PaddleRecognizer::Line PaddleRecognizer::recognize(const QImage& image) const {
    if (!model_ || image.isNull()) return {};
    int width = 0;
    std::vector<float> data = inputTensor(image, width);
    const std::array<int64_t, 4> shape{1, 3, 48, width};
    try {
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, data.data(), data.size(), shape.data(), shape.size());
        const char* inputs[] = {model_->input.c_str()};
        const char* outputs[] = {model_->output.c_str()};
        auto result = model_->session.Run(Ort::RunOptions{nullptr}, inputs, &input, 1, outputs, 1);
        const auto out = result[0].GetTensorTypeAndShapeInfo().GetShape(); // [1, steps, classes]
        if (out.size() != 3) return {};
        auto line = decode(result[0].GetTensorData<float>(), int(out[1]), int(out[2]), model_->dictionary);
        if (model_->arabic) line.text = arabicLogicalOrder(line.text);
        return line;
    } catch (const Ort::Exception&) {
        return {};
    }
}

struct PaddleDetector::Model {
    Ort::Session session{nullptr};
    std::string input, output;
};
PaddleDetector::PaddleDetector() = default;
PaddleDetector::~PaddleDetector() = default;

bool PaddleDetector::load(const QString& modelPath, QString& error) {
    try {
        auto model = std::make_unique<Model>();
        model->session = Ort::Session(environment(), modelPath.toStdWString().c_str(), sessionOptions());
        Ort::AllocatorWithDefaultOptions allocator;
        model->input = model->session.GetInputNameAllocated(0, allocator).get();
        model->output = model->session.GetOutputNameAllocated(0, allocator).get();
        model_ = std::move(model);
        return true;
    } catch (const Ort::Exception& e) {
        error = QStringLiteral("글자 위치 모델을 불러오지 못했습니다 (%1): %2").arg(QFileInfo(modelPath).fileName(), QString::fromUtf8(e.what()));
        return false;
    }
}

std::vector<float> PaddleDetector::inputTensor(const QImage& source, int& width, int& height) {
    // Measured on game-like dialog boxes: 1.5x caught name tags and line starts that 1x missed
    // (CER 7.9% -> 7.3%); 2x was no better and slower. RapidOCR's 736 px shorter side was 2.5x
    // slower and worse. Large selections stay near native scale to bound the time.
    constexpr double Scale = 1.5;
    constexpr int MinSide = 64, MaxSide = 2560;
    constexpr double MaxPixels = 2'000'000;
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    if (w < 1 || h < 1) return {};
    double ratio = std::max(Scale, double(MinSide) / std::min(w, h));
    ratio = std::min(ratio, std::sqrt(MaxPixels / (double(w) * h)));
    if (std::max(w, h) * ratio > MaxSide) ratio = double(MaxSide) / std::max(w, h);
    // Python: int(side * ratio), then round(x / 32) * 32 with halves to even.
    height = std::max(32, int(std::nearbyint(int(h * ratio) / 32.0)) * 32);
    width = std::max(32, int(std::nearbyint(int(w * ratio) / 32.0)) * 32);
    const QImage scaled = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB32);
    std::vector<float> data(size_t(3) * width * height, 0.0f);
    writePlanes(scaled, width, height, data);
    return data;
}

QList<QRect> PaddleDetector::boxes(const float* probabilities, int width, int height, const QSize& original) {
    constexpr float Threshold = 0.3f;
    constexpr double BoxThreshold = 0.5, UnclipRatio = 1.6;
    constexpr int MinSize = 3, MaxCandidates = 1000;
    auto text = [&](int x, int y) { return x >= 0 && y >= 0 && probabilities[size_t(y) * width + x] > Threshold; };
    // 2x2 dilation (cv2.dilate anchors the kernel at (1, 1), so it looks up and to the left).
    std::vector<unsigned char> mask(size_t(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            mask[size_t(y) * width + x] = text(x, y) || text(x - 1, y) || text(x, y - 1) || text(x - 1, y - 1);
    QList<QRect> found;
    std::vector<int> stack;
    int candidates = 0;
    for (int start = 0; start < width * height && candidates < MaxCandidates; ++start) {
        if (!mask[size_t(start)]) continue;
        ++candidates;
        // Flood fill one 8-connected region and keep its bounding box.
        int left = width, right = -1, top = height, bottom = -1;
        stack.assign(1, start);
        mask[size_t(start)] = 0;
        while (!stack.empty()) {
            const int at = stack.back(); stack.pop_back();
            const int x = at % width, y = at / width;
            left = std::min(left, x); right = std::max(right, x);
            top = std::min(top, y); bottom = std::max(bottom, y);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height || !mask[size_t(ny) * width + nx]) continue;
                    mask[size_t(ny) * width + nx] = 0;
                    stack.push_back(ny * width + nx);
                }
        }
        const int boxWidth = right - left, boxHeight = bottom - top; // between pixel centres, like cv2.minAreaRect
        if (std::min(boxWidth, boxHeight) < MinSize) continue;
        double sum = 0;
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x) sum += probabilities[size_t(y) * width + x];
        if (sum / ((boxWidth + 1.0) * (boxHeight + 1.0)) < BoxThreshold) continue;
        // The model marks a shrunken text core; grow it back by area * ratio / perimeter.
        const double grow = double(boxWidth) * boxHeight * UnclipRatio / (2.0 * (boxWidth + boxHeight));
        if (std::min(boxWidth, boxHeight) + 2 * grow < MinSize + 2) continue;
        auto scaleX = [&](double x) { return std::clamp(int(std::nearbyint(x / width * original.width())), 0, original.width() - 1); };
        auto scaleY = [&](double y) { return std::clamp(int(std::nearbyint(y / height * original.height())), 0, original.height() - 1); };
        const QRect box(QPoint(scaleX(left - grow), scaleY(top - grow)), QPoint(scaleX(right + grow), scaleY(bottom + grow)));
        if (box.width() - 1 <= 3 || box.height() - 1 <= 3) continue;
        found << box;
    }
    return found;
}

QList<QRect> PaddleDetector::joinLines(QList<QRect> boxes) {
    std::sort(boxes.begin(), boxes.end(), [](const QRect& a, const QRect& b) { return a.center().y() < b.center().y(); });
    QList<QRect> lines;
    for (const QRect& box : boxes) {
        // Words of one line overlap vertically by more than half the smaller height.
        if (!lines.isEmpty()) {
            QRect& last = lines.last();
            const int overlap = std::min(last.bottom(), box.bottom()) - std::max(last.top(), box.top()) + 1;
            if (overlap * 2 > std::min(last.height(), box.height())) { last = last.united(box); continue; }
        }
        lines << box;
    }
    return lines;
}

QList<QRect> PaddleDetector::lines(const QImage& region) const {
    if (!model_ || region.isNull()) return {};
    int width = 0, height = 0;
    std::vector<float> data = inputTensor(region, width, height);
    if (data.empty()) return {};
    const std::array<int64_t, 4> shape{1, 3, height, width};
    try {
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, data.data(), data.size(), shape.data(), shape.size());
        const char* inputs[] = {model_->input.c_str()};
        const char* outputs[] = {model_->output.c_str()};
        auto result = model_->session.Run(Ort::RunOptions{nullptr}, inputs, &input, 1, outputs, 1);
        const auto out = result[0].GetTensorTypeAndShapeInfo().GetShape(); // [1, 1, height, width]
        if (out.size() != 4 || out[2] != height || out[3] != width) return {};
        return joinLines(boxes(result[0].GetTensorData<float>(), width, height, region.size()));
    } catch (const Ort::Exception&) {
        return {};
    }
}

QList<QImage> textLines(const QImage& region, bool tallMarks) {
    const QImage image = region.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    if (w < 2 || h < 2) return {region};
    std::vector<int> grey(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) grey[size_t(y) * w + x] = luma(row[x]);
    }
    // Strong horizontal or vertical steps in brightness: text strokes are full of them.
    std::vector<unsigned char> strong(size_t(w) * h, 0);
    std::vector<int> rows(h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int edge = x > 0 ? std::abs(grey[size_t(y) * w + x] - grey[size_t(y) * w + x - 1]) : 0;
            if (y > 0) edge = std::max(edge, std::abs(grey[size_t(y) * w + x] - grey[size_t(y - 1) * w + x]));
            if (edge > 60) { strong[size_t(y) * w + x] = 1; ++rows[y]; }
        }
    const int peak = *std::max_element(rows.begin(), rows.end());
    if (peak == 0) return {region};
    std::vector<std::pair<int, int>> bands;
    int start = -1;
    for (int y = 0; y <= h; ++y) {
        const bool on = y < h && rows[y] > peak * 0.25;
        if (on && start < 0) start = y;
        if (!on && start >= 0) { if (y - start >= 4) bands.emplace_back(start, y); start = -1; }
    }
    // Join pieces of one line split by a thin gap (less than a third of the line height).
    std::vector<std::pair<int, int>> merged;
    for (const auto& band : bands) {
        if (!merged.empty() && band.first - merged.back().second < std::max(3, (merged.back().second - merged.back().first) / 3))
            merged.back().second = band.second;
        else merged.push_back(band);
    }
    QList<QImage> lines;
    for (const auto& [top, bottom] : merged) {
        int left = w, right = -1;
        for (int y = top; y < bottom; ++y)
            for (int x = 0; x < w; ++x)
                if (strong[size_t(y) * w + x]) { left = std::min(left, x); right = std::max(right, x); }
        if (right < 0) continue;
        const int pad = std::max(3, (bottom - top) / (tallMarks ? 1 : 4));
        const QRect box(QPoint(std::max(0, left - pad), std::max(0, top - pad)),
                        QPoint(std::min(w - 1, right + pad), std::min(h - 1, bottom + pad - 1)));
        lines << region.copy(box);
    }
    if (lines.isEmpty()) lines << region;
    return lines;
}

QString arabicLogicalOrder(const QString& visual) {
    if (std::none_of(visual.begin(), visual.end(), [](QChar c) { return c.script() == QChar::Script_Arabic; }))
        return visual;
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<UBiDi, decltype(&ubidi_close)> bidi(ubidi_openSized(int32_t(visual.size()), 0, &status), ubidi_close);
    if (U_FAILURE(status) || !bidi) return {};
    // Inverse bidi preserves LTR digit/Latin runs, unlike reversing the string.
    ubidi_setInverse(bidi.get(), true);
    ubidi_setPara(bidi.get(), reinterpret_cast<const UChar*>(visual.utf16()), int32_t(visual.size()),
                  UBIDI_RTL, nullptr, &status);
    QString logical(visual.size() * 2 + 1, QChar());
    const int32_t count = ubidi_writeReordered(bidi.get(), reinterpret_cast<UChar*>(logical.data()),
        int32_t(logical.size()), UBIDI_DO_MIRRORING, &status);
    if (U_FAILURE(status)) return {};
    logical.resize(count);
    return logical;
}

QStringList PaddleOcr::modelFiles(const QString& languages) {
    if (!Languages::isOcrCode(languages)) return {};
    if (languages == "multi") return {QLatin1String(KoreanModel), QLatin1String(CjkModel),
        QLatin1String(LatinModel), QLatin1String(CyrillicModel), QLatin1String(ThaiModel),
        QLatin1String(ArabicModel), QLatin1String(HindiModel)};
    // This published Latin model cannot emit several Vietnamese tone marks. Use Tesseract vie instead.
    if (languages == "vie") return {};
    if (languages == "kor") return {QLatin1String(KoreanModel)};
    if (languages == "fra" || languages == "deu" || languages == "spa" || languages == "por")
        return {QLatin1String(LatinModel)};
    if (languages == "rus") return {QLatin1String(CyrillicModel)};
    if (languages == "tha") return {QLatin1String(ThaiModel)};
    if (languages == "ara") return {QLatin1String(ArabicModel)};
    if (languages == "hin") return {QLatin1String(HindiModel)};
    if (languages.contains("kor")) return {QLatin1String(KoreanModel), QLatin1String(CjkModel)};
    return {QLatin1String(CjkModel)};
}
bool PaddleOcr::available(const QString& modelDir, const QString& languages) {
    const QStringList files = modelFiles(languages);
    if (modelDir.isEmpty() || files.isEmpty()) return false;
    const QDir dir(modelDir);
    return std::all_of(files.begin(), files.end(), [&](const QString& file) {
        const QFileInfo info(dir.filePath(file));
        return info.isFile() && info.size() > 0;
    });
}
namespace {
int letterCount(const QString& text) {
    int letters = 0;
    for (const QChar c : text) letters += c.isLetterOrNumber();
    return letters;
}
bool latinOnly(const QString& text) {
    bool any = false;
    for (const QChar c : text) {
        if (!c.isLetter()) continue;
        if (c.script() != QChar::Script_Latin) return false;
        any = true;
    }
    return any;
}
struct Reading { QString file; PaddleRecognizer::Line line; };
// A model's confidence averages only the characters it kept, so one that keeps just "?" looks
// certain (measured: the Korean model beat the Chinese one that way). Weight each reading by
// how much of the line it read compared with the other models.
double choiceScore(const Reading& reading, int mostLetters) {
    if (mostLetters == 0) return reading.line.confidence;
    return reading.line.confidence * std::sqrt(double(letterCount(reading.line.text)) / mostLetters);
}
const Reading& choose(const std::vector<Reading>& readings, double* score = nullptr) {
    int mostLetters = 0;
    for (const auto& r : readings) mostLetters = std::max(mostLetters, letterCount(r.line.text));
    size_t best = 0;
    double bestScore = -1;
    for (size_t i = 0; i < readings.size(); ++i) {
        const double s = choiceScore(readings[i], mostLetters);
        if (s > bestScore) { bestScore = s; best = i; }
    }
    if (score) *score = bestScore;
    return readings[best];
}
}
PaddleOcr::Result PaddleOcr::read(const QImage& region, const QString& languages, QString& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    const QStringList files = modelFiles(languages);
    if (files.isEmpty()) { error = QStringLiteral("지원하지 않는 OCR 언어입니다."); return {}; }
    // Release models no longer selected instead of retaining all seven after switching languages.
    for (auto it = models_.begin(); it != models_.end();)
        if (!files.contains(it->first)) it = models_.erase(it); else ++it;
    auto model = [&](const QString& file) -> PaddleRecognizer* {
        auto found = models_.find(file);
        if (found != models_.end()) return found->second.get();
        auto loaded = std::make_unique<PaddleRecognizer>(); // loaded on first use
        if (!loaded->load(QDir(dir_).filePath(file), error)) return nullptr;
        return models_.emplace(file, std::move(loaded)).first->second.get();
    };
    // Multi-language auto-detect reads with the Korean and Chinese/Japanese/English models first
    // and asks the other five only when that reading is doubtful; Latin-only text also gets the
    // Latin and Cyrillic models, so accents are kept and Russian is not mistaken for Latin.
    const bool staged = languages == QLatin1String("multi");
    Result result;
    QStringList texts;
    double weighted = 0;
    qsizetype letters = 0;
    // The detection model is optional: older installs without it keep the edge-band line finder.
    if (!detectorTried_) {
        detectorTried_ = true;
        const QString path = QDir(dir_).filePath(QLatin1String(DetectionModel));
        auto loaded = std::make_unique<PaddleDetector>();
        QString ignored;
        if (QFileInfo(path).isFile() && loaded->load(path, ignored)) detector_ = std::move(loaded);
    }
    QList<QImage> lines, taller;
    // Crops as RapidOCR makes them: extra padding was measured as no better (horizontal) or worse (vertical).
    if (detector_)
        for (const QRect& box : detector_->lines(region)) lines << region.copy(box);
    result.detected = !lines.isEmpty();
    if (!result.detected) {
        lines = textLines(region);
        if (files.contains(QLatin1String(HindiModel))) taller = textLines(region, true);
    }
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QImage& line = lines[index];
        if (line.isNull()) continue;
        if (double(line.width()) / std::max(1, line.height()) > 8192.0 / 48) {
            error = QStringLiteral("자막 영역이 너무 길고 얇습니다. 한두 줄의 글자와 여백을 함께 선택하세요.");
            return {};
        }
        std::vector<Reading> readings;
        auto run = [&](const QString& file) {
            if (std::any_of(readings.begin(), readings.end(), [&](const Reading& r) { return r.file == file; })) return true;
            PaddleRecognizer* recognizer = model(file);
            if (!recognizer) return false;
            // Devanagari vowel signs can sit above the dense stroke band; keep enough vertical padding.
            const QImage& input = file == QLatin1String(HindiModel) && index < taller.size() ? taller[index] : line;
            readings.push_back({file, recognizer->recognize(input)});
            ++result.modelRuns;
            return true;
        };
        if (!staged) {
            for (const QString& file : files) if (!run(file)) return {};
        } else {
            if (!run(QLatin1String(KoreanModel)) || !run(QLatin1String(CjkModel))) return {};
            double score = 0;
            const Reading first = choose(readings, &score);
            if (score < StageTwoBelow) {
                for (const QString& file : files) if (!run(file)) return {};
            } else if (latinOnly(first.line.text)) {
                // Cyrillic text often reads as confident look-alike Latin ("CoxpaHuTb" for "Сохранить").
                if (!run(QLatin1String(LatinModel)) || !run(QLatin1String(CyrillicModel))) return {};
            }
        }
        const QString text = choose(readings).line.text.trimmed();
        if (text.isEmpty()) continue;
        texts << text;
        weighted += choose(readings).line.confidence * text.size(); letters += text.size();
    }
    result.text = texts.join(QLatin1Char('\n'));
    result.confidence = letters ? weighted / letters : 0;
    return result;
}
