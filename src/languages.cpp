#include "languages.h"
#include "subtitle/text.h"
namespace {
template <std::size_t N>
QString labelFor(const std::array<Languages::Option, N>& options, const QString& code) {
    for (const auto& option : options)
        if (code == QLatin1String(option.code)) return QString::fromUtf8(option.label);
    return code;
}
template <std::size_t N>
bool contains(const std::array<Languages::Option, N>& options, const QString& code) {
    for (const auto& option : options)
        if (code == QLatin1String(option.code)) return true;
    return false;
}
}
namespace Languages {
// Only listed codes reach Tesseract's command line or the DeepL request.
bool isOcrCode(const QString& code) { return contains(Ocr, code); }
bool isTargetCode(const QString& code) { return contains(Target, code); }
QString ocrLabel(const QString& code) { return labelFor(Ocr, code); }
QString targetLabel(const QString& code) { return labelFor(Target, code); }
QString deeplSource(const QString& ocrCode) {
    if (ocrCode == "jpn") return "JA";
    if (ocrCode == "eng") return "EN";
    if (ocrCode == "kor") return "KO";
    if (ocrCode == "chi_sim" || ocrCode == "chi_tra") return "ZH";
    const std::pair<const char*, const char*> extra[] = {
        {"fra", "FR"}, {"deu", "DE"}, {"spa", "ES"}, {"por", "PT"}, {"vie", "VI"},
        {"rus", "RU"}, {"tha", "TH"}, {"ara", "AR"}, {"hin", "HI"}};
    for (const auto& [ocr, source] : extra) if (ocrCode == QLatin1String(ocr)) return QLatin1String(source);
    return {};
}
QString sourceForScripts(unsigned scripts, const QString& ocrCode) {
    const QString fixed = deeplSource(ocrCode);
    if (!fixed.isEmpty()) return fixed; // the user chose one language
    if (scripts == subtitle::Latin && ocrCode != "multi") return "EN"; // limited JA/EN/KO mode only
    if (scripts == subtitle::Thai) return "TH";
    if (scripts == subtitle::Cyrillic) return "RU"; // only Russian is offered in this selection
    if (scripts == subtitle::Devanagari) return "HI";
    if (scripts == subtitle::Hangul) return "KO";
    if ((scripts & subtitle::Kana) && !(scripts & ~(subtitle::Kana | subtitle::Han))) return "JA";
    // The shared CJK model reads Chinese even in the default selection. Han alone is ambiguous.
    if (scripts == subtitle::Han) return {};
    return {};
}
QString languageName(const QString& deeplCode) {
    const QString base = deeplCode.section('-', 0, 0).toUpper();
    if (base == "JA") return QStringLiteral("일본어");
    if (base == "EN") return QStringLiteral("영어");
    if (base == "KO") return QStringLiteral("한국어");
    if (base == "ZH") return QStringLiteral("중국어");
    const std::pair<const char*, const char*> extra[] = {
        {"FR", "프랑스어"}, {"DE", "독일어"}, {"ES", "스페인어"}, {"PT", "포르투갈어"},
        {"VI", "베트남어"}, {"RU", "러시아어"}, {"TH", "태국어"}, {"AR", "아랍어"}, {"HI", "힌디어"}};
    for (const auto& [code, name] : extra) if (base == QLatin1String(code)) return QString::fromUtf8(name);
    return deeplCode;
}
bool sameLanguage(const QString& detected, const QString& target) {
    return !detected.isEmpty() &&
           detected.section('-', 0, 0).compare(target.section('-', 0, 0), Qt::CaseInsensitive) == 0;
}
}
