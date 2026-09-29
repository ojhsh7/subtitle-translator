#include "languages.h"
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
    return {};
}
QString languageName(const QString& deeplCode) {
    const QString base = deeplCode.section('-', 0, 0).toUpper();
    if (base == "JA") return QStringLiteral("일본어");
    if (base == "EN") return QStringLiteral("영어");
    if (base == "KO") return QStringLiteral("한국어");
    if (base == "ZH") return QStringLiteral("중국어");
    return deeplCode;
}
bool sameLanguage(const QString& detected, const QString& target) {
    return !detected.isEmpty() &&
           detected.section('-', 0, 0).compare(target.section('-', 0, 0), Qt::CaseInsensitive) == 0;
}
}
