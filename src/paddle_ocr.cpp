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
// Pillow's greyscale conversion (ITU-R 601-2), so line finding matches the measured benchmark.
int luma(QRgb p) { return (qRed(p) * 19595 + qGreen(p) * 38470 + qBlue(p) * 7471 + 0x8000) >> 16; }
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
        Ort::SessionOptions options;
        // Each cached model owns a thread pool. Bound it and stop idle spinning while gaming.
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        options.AddConfigEntry("session.inter_op.allow_spinning", "0");
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        model->session = Ort::Session(environment(), modelPath.toStdWString().c_str(), options);
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
    const size_t plane = size_t(Height) * width;
    for (int y = 0; y < Height; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
        for (int x = 0; x < scaled.width(); ++x) {
            const size_t at = size_t(y) * width + x;
            data[at] = qBlue(row[x]) / 127.5f - 1.0f; // BGR, as the model was trained with OpenCV images
            data[plane + at] = qGreen(row[x]) / 127.5f - 1.0f;
            data[2 * plane + at] = qRed(row[x]) / 127.5f - 1.0f;
        }
    }
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
PaddleOcr::Result PaddleOcr::read(const QImage& region, const QString& languages, QString& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    const QStringList files = modelFiles(languages);
    if (files.isEmpty()) { error = QStringLiteral("지원하지 않는 OCR 언어입니다."); return {}; }
    // Release models no longer selected instead of retaining all seven after switching languages.
    for (auto it = models_.begin(); it != models_.end();)
        if (!files.contains(it->first)) it = models_.erase(it); else ++it;
    for (const QString& file : files) {
        if (models_.count(file)) continue;
        auto model = std::make_unique<PaddleRecognizer>();
        if (!model->load(QDir(dir_).filePath(file), error)) return {};
        models_.emplace(file, std::move(model));
    }
    Result result;
    QStringList texts;
    double weighted = 0;
    qsizetype letters = 0;
    const auto lines = textLines(region);
    const auto taller = files.contains(QLatin1String(HindiModel)) ? textLines(region, true) : QList<QImage>();
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QImage& line = lines[index];
        if (line.isNull()) continue;
        if (double(line.width()) / std::max(1, line.height()) > 8192.0 / 48) {
            error = QStringLiteral("자막 영역이 너무 길고 얇습니다. 한두 줄의 글자와 여백을 함께 선택하세요.");
            return {};
        }
        // Confidence is not calibrated across models: explicit language selection is preferable.
        PaddleRecognizer::Line best;
        for (const QString& file : files) {
            // Devanagari vowel signs can sit above the dense stroke band; keep enough vertical padding.
            const QImage& input = file == QLatin1String(HindiModel) && index < taller.size() ? taller[index] : line;
            const auto other = models_.at(file)->recognize(input);
            ++result.modelRuns;
            if (other.confidence > best.confidence) best = other;
        }
        const QString text = best.text.trimmed();
        if (text.isEmpty()) continue;
        texts << text;
        weighted += best.confidence * text.size(); letters += text.size();
    }
    result.text = texts.join('\n');
    result.confidence = letters ? weighted / letters : 0;
    return result;
}
