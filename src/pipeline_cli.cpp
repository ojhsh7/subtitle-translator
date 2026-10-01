#include "ocr_service.h"
#include "translation_service.h"
#include "credential_store.h"
#include "languages.h"
#include "subtitle/core.h"
#include "subtitle/text.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <windows.h>
#include <psapi.h>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{"i","input"}, "Saved image to OCR.", "path"},
        {"tesseract", "Path to tesseract.exe.", "path"},
        {"tessdata", "Folder containing the traineddata models.", "path"},
        {"lang", "OCR language code from the app list, or multi (default jpn+eng+kor).", "code", Languages::DefaultOcr},
        {"target", "DeepL target language (default KO).", "code", Languages::DefaultTarget},
        {"translate", "Send OCR text to DeepL using the user's credential or SUBTITLE_TRANSLATOR_API_KEY."},
        {"pro", "Use the DeepL API Pro endpoint."},
        {"paddle", "Folder with the PaddleOCR models (default: ocr/paddle next to this program).", "path"},
        {"engine", "OCR engine: paddle (default, falls back to Tesseract) or tesseract.", "name", "paddle"}
    });
    parser.process(app);
    if (!parser.isSet("input")) parser.showHelp(2); // --tesseract is optional: it is the fallback engine
    const QString languages = parser.value("lang"), target = parser.value("target");
    if (!Languages::isOcrCode(languages) || !Languages::isTargetCode(target)) {
        QTextStream(stderr) << "Unsupported --lang or --target\n"; return 2;
    }
    QImageReader reader(parser.value("input")); reader.setAutoTransform(true);
    if (!reader.size().isValid() || qint64(reader.size().width()) * reader.size().height() > 16000000) {
        QTextStream(stderr) << "Invalid or oversized image\n"; return 2;
    }
    const QImage image = reader.read();
    if (image.isNull()) { QTextStream(stderr) << "Cannot decode image\n"; return 2; }
    st_session* session = st_create();
    const auto request = st_begin(session, ST_IMAGE_FILE, image.width(), image.height());
    OcrService ocr;
    const QString paddleDir = parser.isSet("paddle") ? parser.value("paddle")
                                                    : QCoreApplication::applicationDirPath() + "/ocr/paddle";
    if (parser.value("engine") != "tesseract") ocr.setPaddleModelDir(paddleDir);
    TranslationService translation;
    QElapsedTimer total, phase;
    total.start(); phase.start();
    QJsonObject report{
        {"schema", "subtitle.pipeline.v1"}, {"measurementPlatform", "Windows host"},
        {"inputType", "saved-image"}, {"width", image.width()}, {"height", image.height()},
        {"ocrLanguages", languages}, {"targetLanguage", target},
        {"imageUploaded", false}, {"translationNetworkUsed", false},
        {"mobilePerformance", "Not measured; no mobile estimate provided"},
        {"ocrTestData", "Actual image input; no synthetic OCR result"}
    };
    std::vector<subtitle::TextSegment> segments;
    unsigned scripts = 0;
    bool done = false;
    auto finish = [&](int exitCode) {
        if (done) return;
        done = true;
        report["totalMs"] = total.elapsed();
        PROCESS_MEMORY_COUNTERS memory{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
            report["hostPeakWorkingSetBytes"] = qint64(memory.PeakWorkingSetSize);
        report["memoryNote"] = "Host peak working set includes Qt; OCR child measured separately when available.";
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
        app.exit(exitCode);
    };
    QObject::connect(&ocr, &OcrService::failed, &app, [&](const QString& error) {
        st_fail(session, request);
        report["state"] = "failed"; report["stage"] = "ocr"; report["message"] = error;
        finish(3);
    });
    QObject::connect(&ocr, &OcrService::recognized, &app, [&](const QString& text) {
        report["ocrMs"] = phase.elapsed();
        report["ocrAttempts"] = ocr.attemptsUsed();
        report["ocrEngine"] = ocr.engineUsed();
        report["ocrConfidence"] = ocr.confidence();
        report["ocrReadingRejected"] = ocr.readingRejected();
        report["ocrChildPeakWorkingSetBytes"] = qint64(ocr.peakWorkingSetBytes());
        const int decision = st_accept_ocr(session, request, text.toUtf8().constData());
        report["sourceText"] = QString::fromUtf8(st_source_text(session));
        if (decision != ST_TRANSLATE) {
            report["state"] = "failed"; report["stage"] = "ocr"; report["message"] = ocr.readingRejected() ? "Low-confidence OCR output rejected" : "Empty or oversized OCR output"; finish(3); return;
        }
        // Same rule as the app: only foreign-script runs go to DeepL.
        segments = subtitle::splitForTranslation(st_source_text(session),
            subtitle::preservableScripts(target.section('-', 0, 0).toLower().toStdString(),
                Languages::deeplSource(languages).toLower().toStdString()));
        QStringList pieces;
        for (const auto& segment : segments)
            if (segment.translate) { pieces << QString::fromStdString(segment.utf8); scripts |= subtitle::scriptsIn(segment.utf8); }
        report["piecesToTranslate"] = int(pieces.size());
        if (!parser.isSet("translate")) {
            st_finish_ocr(session, request);
            report["state"] = "ocr_complete_translation_not_requested";
            finish(0); return;
        }
        if (pieces.isEmpty()) {
            st_finish_ocr(session, request);
            report["state"] = "already_target_language";
            report["message"] = "No foreign-script text; no translation request sent.";
            finish(0); return;
        }
        QString error;
        QString key = qEnvironmentVariable("SUBTITLE_TRANSLATOR_API_KEY");
        if (key.isEmpty()) key = Credentials::load(error);
        if (key.isEmpty()) {
            st_finish_ocr(session, request);
            report["state"] = "ocr_complete_translation_unverified";
            report["message"] = "No user API key available; no translation request sent.";
            finish(4); return;
        }
        phase.restart();
        report["translationNetworkUsed"] = true;
        report["outboundTextUtf8Bytes"] = int(pieces.join(QString()).toUtf8().size());
        report["networkMeasurementNote"] = "Text payload only; TLS and HTTP transport bytes are not measured.";
        const QString source = Languages::sourceForScripts(scripts, languages);
        report["sourceLanguageSent"] = source.isEmpty() ? QStringLiteral("auto") : source;
        translation.translate(pieces, key, !parser.isSet("pro"), target, source);
        key.fill(QChar('\0'));
    });
    QObject::connect(&translation, &TranslationService::translated, &app, [&](const QStringList& translated, const QString& detected) {
        std::vector<std::string> parts;
        for (const QString& piece : translated) parts.push_back(piece.toStdString());
        const std::string joined = subtitle::joinTranslated(segments, parts);
        report["translationMs"] = phase.elapsed();
        if (joined.empty()) {
            st_fail(session, request);
            report["state"] = "failed"; report["stage"] = "translation"; report["message"] = "Translation count mismatch"; finish(5); return;
        }
        st_complete(session, request, joined.c_str());
        report["state"] = "complete";
        report["translatedText"] = QString::fromStdString(joined); report["detectedSourceLanguage"] = detected; finish(0);
    });
    QObject::connect(&translation, &TranslationService::failed, &app, [&](const QString& error) {
        st_fail(session, request);
        report["translationMs"] = phase.elapsed(); report["state"] = "failed";
        report["stage"] = "translation"; report["message"] = error; finish(5);
    });
    QTimer::singleShot(0, &app, [&] {
        ocr.recognize(image, parser.value("tesseract"), parser.value("tessdata"), 0, languages);
    });
    QTimer::singleShot(60000, &app, [&] {
        report["state"] = "timeout"; ocr.cancel(); translation.cancel(); finish(6);
    });
    const int exitCode = app.exec();
    st_destroy(session);
    return exitCode;
}
