#pragma once
#include <QString>
#include <array>

// Windows adapter language tables: Tesseract model codes (OCR) and DeepL target codes.
namespace Languages {
struct Option { const char* label; const char* code; };
// "+" loads several Tesseract models; the engine picks the best match per word,
// so a combined code doubles as automatic language detection.
inline constexpr std::array<Option, 7> Ocr{{
    {"자동 감지 · 일본어·영어·한국어", "jpn+eng+kor"},
    {"자동 감지 · 일본어·영어·한국어·중국어(간체)", "jpn+eng+kor+chi_sim"},
    {"일본어", "jpn"},
    {"영어", "eng"},
    {"한국어", "kor"},
    {"중국어(간체)", "chi_sim"},
    {"중국어(번체)", "chi_tra"},
}};
inline constexpr std::array<Option, 6> Target{{
    {"한국어", "KO"},
    {"영어(미국)", "EN-US"},
    {"영어(영국)", "EN-GB"},
    {"일본어", "JA"},
    {"중국어(간체)", "ZH-HANS"},
    {"중국어(번체)", "ZH-HANT"},
}};
inline constexpr const char* DefaultOcr = "jpn+eng+kor";
inline constexpr const char* DefaultTarget = "KO";
bool isOcrCode(const QString& code);
bool isTargetCode(const QString& code);
QString ocrLabel(const QString& code);
QString targetLabel(const QString& code);
// DeepL source_lang for a single-language OCR choice; empty lets DeepL detect the language.
QString deeplSource(const QString& ocrCode);
// DeepL source_lang for the pieces being sent, from their scripts (subtitle::Script bits).
// DeepL's own detection is unreliable for short fragments such as "you.", so when the letters
// can only come from one of our OCR models (Latin -> English, kana -> Japanese, Hangul -> Korean)
// we say so. Returns empty when mixed or ambiguous (e.g. kanji with both jpn and chi loaded).
QString sourceForScripts(unsigned scripts, const QString& ocrCode);
// Korean name for a DeepL language code such as "EN" or "EN-US".
QString languageName(const QString& deeplCode);
// True when DeepL detected the language we are translating into (e.g. "EN" and "EN-US").
bool sameLanguage(const QString& detected, const QString& target);
}
