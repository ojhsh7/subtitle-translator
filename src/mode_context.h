#pragma once
#include <QRect>
#include <QString>
#include <functional>
class OcrService;
class TranslationQueue;
class SubtitlePresenter;
class HistoryStore;
class UsageMeter;

// What a mode page may use from the app window. Only one screen-watching mode runs at a time, so
// the OCR engine (and its loaded models) and the caption window are shared.
struct OcrSetup {
    QString executable, tessdata;
    int psm = 0;
    QString languages;
};
struct ModeContext {
    OcrService* ocr = nullptr;
    TranslationQueue* queue = nullptr;
    SubtitlePresenter* presenter = nullptr;
    HistoryStore* history = nullptr;
    UsageMeter* usage = nullptr;
    std::function<OcrSetup()> ocrSetup;
    std::function<bool()> ocrReady;
    std::function<QString()> sourceLanguages; // OCR code of the active mode, e.g. "jpn+eng+kor"
    std::function<QString()> targetLanguage;  // DeepL code, e.g. "KO"
    std::function<QString()> translationBlocked; // empty when a translation may be sent, else the reason
    std::function<void(const QString&)> status;
    std::function<void()> hideOwnWindows;    // before a screen snapshot for region selection
    std::function<void()> restoreOwnWindows;
    std::function<void()> minimizeMain;      // keep the app window off the watched area
    std::function<bool(const QRect&)> ownWindowCovers; // app windows that would be captured
    std::function<void()> showHistory;
};
