#include "ocr_service.h"
#include "translation_service.h"
#include "credential_store.h"
#include "subtitle/core.h"
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
        {"tessdata", "Folder containing jpn.traineddata.", "path"},
        {"translate", "Send OCR text to DeepL using the user's credential or SUBTITLE_TRANSLATOR_API_KEY."},
        {"pro", "Use the DeepL API Pro endpoint."}
    });
    parser.process(app);
    if (!parser.isSet("input") || !parser.isSet("tesseract")) parser.showHelp(2);
    QImageReader reader(parser.value("input")); reader.setAutoTransform(true);
    if (!reader.size().isValid() || qint64(reader.size().width()) * reader.size().height() > 16000000) {
        QTextStream(stderr) << "Invalid or oversized image\n"; return 2;
    }
    const QImage image = reader.read();
    if (image.isNull()) { QTextStream(stderr) << "Cannot decode image\n"; return 2; }
    st_session* session = st_create();
    const auto request = st_begin(session, ST_IMAGE_FILE, image.width(), image.height());
    OcrService ocr;
    TranslationService translation;
    QElapsedTimer total, phase;
    total.start(); phase.start();
    QJsonObject report{
        {"schema", "subtitle.pipeline.v1"}, {"measurementPlatform", "Windows host"},
        {"inputType", "saved-image"}, {"width", image.width()}, {"height", image.height()},
        {"ocrEngine", "Tesseract CLI"}, {"modelBytes", QFileInfo(parser.value("tessdata") + "/jpn.traineddata").size()},
        {"imageUploaded", false}, {"translationNetworkUsed", false},
        {"mobilePerformance", "Not measured; no mobile estimate provided"},
        {"ocrTestData", "Actual image input; no synthetic OCR result"}
    };
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
        report["ocrChildPeakWorkingSetBytes"] = qint64(ocr.peakWorkingSetBytes());
        const int decision = st_accept_ocr(session, request, text.toUtf8().constData());
        report["japaneseText"] = QString::fromUtf8(st_source_text(session));
        if (decision != ST_TRANSLATE) {
            report["state"] = "failed"; report["stage"] = "ocr"; report["message"] = "Empty or oversized OCR output"; finish(3); return;
        }
        if (!parser.isSet("translate")) {
            st_finish_ocr(session, request);
            report["state"] = "ocr_complete_translation_not_requested";
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
        report["outboundTextUtf8Bytes"] = text.toUtf8().size();
        report["networkMeasurementNote"] = "Text payload only; TLS and HTTP transport bytes are not measured.";
        translation.translate(QString::fromUtf8(st_source_text(session)), key, !parser.isSet("pro"));
        key.fill(QChar('\0'));
    });
    QObject::connect(&translation, &TranslationService::translated, &app, [&](const QString& text) {
        st_complete(session, request, text.toUtf8().constData());
        report["translationMs"] = phase.elapsed(); report["state"] = "complete";
        report["koreanText"] = text; finish(0);
    });
    QObject::connect(&translation, &TranslationService::failed, &app, [&](const QString& error) {
        st_fail(session, request);
        report["translationMs"] = phase.elapsed(); report["state"] = "failed";
        report["stage"] = "translation"; report["message"] = error; finish(5);
    });
    QTimer::singleShot(0, &app, [&] {
        ocr.recognize(image, parser.value("tesseract"), parser.value("tessdata"), 6);
    });
    QTimer::singleShot(60000, &app, [&] {
        report["state"] = "timeout"; ocr.cancel(); translation.cancel(); finish(6);
    });
    const int exitCode = app.exec();
    st_destroy(session);
    return exitCode;
}
