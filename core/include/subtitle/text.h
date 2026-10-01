#pragma once
#include <string>
#include <vector>
namespace subtitle {
// Writing systems, not language identification.
enum Script : unsigned {
    Hangul = 1, Latin = 2, Kana = 4, Han = 8, Cyrillic = 16, Thai = 32, Arabic = 64, Devanagari = 128,
    OtherLetters = 256 // Greek, Hebrew and other scripts without their own OCR model
};
// "ko", "en", "ja", "zh" (lowercase language prefix). 0 means unknown: translate everything.
unsigned nativeScripts(const std::string& language);
// Scripts safe to preserve without a translation request. Lowercase language prefixes.
// Shared scripts (Latin, Han, etc.) need an explicit matching source language.
// Example: ("en", "fr") and ("en", "") both return 0; ("en", "en") returns Latin.
unsigned preservableScripts(const std::string& target, const std::string& explicitSource);
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
// False for pieces that a translation request cannot improve and that OCR noise often produces:
// no letters at all ("★ ▶ ※", "ー・ー |"), or Latin letters only and at most three of them
// ("O", "×O", "[jrJ[]", "v☆XO△", "Lv.10", "HP"). Such text is kept as it is on screen.
bool worthTranslating(const std::string& utf8);
// Marks unworthy translatable segments as kept text and merges neighbours of the same kind.
// Returns the number of segments that were changed.
int keepUnworthySegments(std::vector<TextSegment>& segments);
}
