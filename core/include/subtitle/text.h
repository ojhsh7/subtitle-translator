#pragma once
#include <string>
#include <vector>
namespace subtitle {
// Writing systems a reader of the target language already understands.
enum Script : unsigned { Hangul = 1, Latin = 2, Kana = 4, Han = 8, OtherLetters = 16 };
// "ko", "en", "ja", "zh" (lowercase language prefix). 0 means unknown: translate everything.
unsigned nativeScripts(const std::string& language);
struct TextSegment {
    std::string utf8;
    bool translate = false;
};
// Splits OCR text so only runs written in a foreign script are sent for translation.
// Text the reader already understands (e.g. Hangul for Korean) is kept as it is.
std::vector<TextSegment> splitForTranslation(const std::string& utf8, unsigned native);
// Script bits of all letters in the text (digits, punctuation and spaces are ignored).
unsigned scriptsIn(const std::string& utf8);
// Rebuilds the text, replacing translatable segments with translations in order.
// Returns an empty string when the number of translations does not match.
std::string joinTranslated(const std::vector<TextSegment>& segments, const std::vector<std::string>& translations);
}
