#pragma once
#include <QString>
#include <array>

// Windows adapter: OCR selection codes and DeepL target language codes.
namespace Languages {
struct Option { const char* label; const char* code; };
// Multi is a Paddle-only experimental selection. Other codes are also Tesseract model codes.
inline constexpr std::array<Option, 17> Ocr{{
    {"자동 감지 · 한국어·일본어·중국어·영어 (권장)", "jpn+eng+kor"},
    {"자동 감지 · 한국어·일본어·중국어·영어 (대체 엔진에 중국어 간체 포함)", "jpn+eng+kor+chi_sim"},
    {"자동 감지 · 다국어 13종 (메모리 많이 사용)", "multi"},
    {"일본어", "jpn"}, {"영어", "eng"}, {"한국어", "kor"},
    {"중국어(간체)", "chi_sim"}, {"중국어(번체)", "chi_tra"},
    {"프랑스어", "fra"}, {"독일어", "deu"}, {"스페인어", "spa"},
    {"포르투갈어", "por"}, {"베트남어 · 전용 Tesseract", "vie"}, {"러시아어", "rus"},
    {"태국어", "tha"}, {"아랍어", "ara"}, {"힌디어", "hin"},
}};
inline constexpr std::array<Option, 6> Target{{
    {"한국어", "KO"}, {"영어(미국)", "EN-US"}, {"영어(영국)", "EN-GB"},
    {"일본어", "JA"}, {"중국어(간체)", "ZH-HANS"}, {"중국어(번체)", "ZH-HANT"},
}};
inline constexpr const char* DefaultOcr = "jpn+eng+kor";
inline constexpr const char* DefaultTarget = "KO";
bool isOcrCode(const QString& code);
bool isTargetCode(const QString& code);
QString ocrLabel(const QString& code);
QString targetLabel(const QString& code);
// Empty source_lang lets DeepL detect the language.
QString deeplSource(const QString& ocrCode);
// A writing system is not a language: multilingual Latin/Han stays ambiguous.
QString sourceForScripts(unsigned scripts, const QString& ocrCode);
QString languageName(const QString& deeplCode);
bool sameLanguage(const QString& detected, const QString& target);
}
