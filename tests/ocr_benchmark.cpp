// Offline synthetic-image benchmark of the exact Windows C++ recognizer. Never calls DeepL.
#include "paddle_ocr.h"
#include "ocr_service.h"
#include <QEventLoop>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"manifest", "Generated fixture manifest.", "file"},
        {"models", "Paddle model directory.", "dir"}, {"output", "JSON report.", "file"},
        {"mode", "selected, legacy or multi.", "mode", "selected"},
        {"tesseract", "Tesseract executable for Vietnamese.", "file"}, {"tessdata", "Tesseract models.", "dir"}});
    parser.process(app);
    const QString mode = parser.value("mode");
    if (mode != "selected" && mode != "legacy" && mode != "multi") return 2;
    QFile manifest(parser.value("manifest"));
    if (!manifest.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) return 2;
    const QDir fixtures(QFileInfo(manifest).absolutePath());
    OcrService ocr; ocr.setPaddleModelDir(parser.value("models"));
    QEventLoop loop;
    QString actual, error;
    bool completed = false;
    QObject::connect(&ocr, &OcrService::recognized, &loop, [&](const QString& text) {
        actual = text; completed = true; loop.quit();
    });
    QObject::connect(&ocr, &OcrService::failed, &loop, [&](const QString& message) {
        error = message; completed = true; loop.quit();
    });
    QJsonArray rows;
    int failures = 0;
    QStringList previousModels;
    QElapsedTimer total; total.start();
    for (const auto value : doc.array()) {
        auto row = value.toObject();
        const QImage image(fixtures.filePath(row["file"].toString()));
        if (image.isNull()) { QTextStream(stderr) << "Cannot decode fixture\n"; return 3; }
        const QString language = mode == "selected" ? row["ocr"].toString()
            : mode == "legacy" ? QStringLiteral("jpn+eng+kor") : QStringLiteral("multi");
        const QStringList selected = PaddleOcr::modelFiles(language);
        row["coldModelLoad"] = selected != previousModels;
        actual.clear(); error.clear(); completed = false;
        QElapsedTimer timer; timer.start();
        ocr.recognize(image, parser.value("tesseract"), parser.value("tessdata"), 0, language);
        if (!completed) loop.exec();
        row["ocrMs"] = timer.nsecsElapsed() / 1000000.0;
        row["actual"] = actual;
        row["engine"] = ocr.engineUsed();
        row["modelRuns"] = ocr.attemptsUsed();
        row["ocrChildPeakWorkingSetBytes"] = qint64(ocr.peakWorkingSetBytes());
        row["error"] = error;
        // Diagnostics for false-text work: what each PaddleOCR line scored, and Tesseract's confidence.
        QJsonArray lines;
        for (const auto& line : ocr.paddleDetails().lines) {
            QJsonArray pieces;
            for (const auto& piece : line.pieces) pieces.append(QJsonArray{piece.text, double(piece.probability)});
            lines.append(QJsonObject{{"text", line.text}, {"confidence", line.confidence},
                                     {"lowestCharacter", line.lowestCharacter}, {"model", line.model}, {"pieces", pieces}});
        }
        row["paddleLines"] = lines;
        row["detectorBoxes"] = ocr.paddleDetails().detectorBoxes;
        row["tesseractConfidence"] = ocr.tesseractConfidence();
        // "Nothing read" is a measured outcome (an empty reading counts as all characters wrong), not a crash.
        if (!error.isEmpty()) ++failures;
        previousModels = selected;
        rows.append(row);
        if (rows.size() % 48 == 0) QTextStream(stderr) << mode << ": " << rows.size() << "/" << doc.array().size() << "\n";
    }
    PROCESS_MEMORY_COUNTERS memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
    // v2: rows also carry paddleLines (text, confidence, per-piece probabilities), detectorBoxes, tesseractConfidence.
    const QJsonObject report{{"schema", "subtitle.ocr-benchmark.v2"}, {"mode", mode},
        {"platform", "Windows x64 CPU, native C++ implementation"}, {"totalMs", total.elapsed()},
        {"peakWorkingSetBytes", qint64(memory.PeakWorkingSetSize)}, {"networkUsed", false}, {"failedReads", failures},
        {"mobileMeasured", false}, {"data", "Synthetic text images; not a real-game accuracy estimate"}, {"rows", rows}};
    QFile output(parser.value("output"));
    if (!output.open(QIODevice::WriteOnly)) return 4;
    output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    QTextStream(stderr) << mode << ": done, " << failures << " of " << rows.size() << " images read nothing\n";
    return 0;
}
