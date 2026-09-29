#include "subtitle/text.h"
#include <cstddef>
namespace subtitle {
namespace {
enum class Kind { Neutral, Native, Foreign };
struct Token {
    std::size_t begin, end; // byte offsets
    Kind kind;
    int letters;
    bool acronym; // only ASCII capitals and digits, e.g. "HP", "NPC"
};
// Decodes one UTF-8 code point; invalid bytes become U+FFFD so they count as neutral.
char32_t decode(const std::string& s, std::size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xe ? 2 : (c >> 3) == 0x1e ? 3 : -1;
    if (extra < 0 || (extra > 0 && i + static_cast<std::size_t>(extra) >= s.size())) { ++i; return 0xfffd; }
    char32_t cp = extra == 0 ? c : c & (0x3f >> extra);
    for (int k = 1; k <= extra; ++k) {
        const auto next = static_cast<unsigned char>(s[i + k]);
        if ((next & 0xc0) != 0x80) { ++i; return 0xfffd; }
        cp = (cp << 6) | (next & 0x3f);
    }
    i += extra + 1;
    return cp;
}
unsigned scriptOf(char32_t c) {
    if ((c >= 0xac00 && c <= 0xd7a3) || (c >= 0x1100 && c <= 0x11ff) || (c >= 0x3131 && c <= 0x318e) ||
        (c >= 0xa960 && c <= 0xa97f) || (c >= 0xd7b0 && c <= 0xd7ff)) return Hangul;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xc0 && c <= 0x24f && c != 0xd7 && c != 0xf7) ||
        (c >= 0xff21 && c <= 0xff3a) || (c >= 0xff41 && c <= 0xff5a)) return Latin;
    if ((c >= 0x3041 && c <= 0x309f) || (c >= 0x30a1 && c <= 0x30ff && c != 0x30fb) ||
        (c >= 0x31f0 && c <= 0x31ff) || (c >= 0xff66 && c <= 0xff9f)) return Kana;
    if ((c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf) || (c >= 0xf900 && c <= 0xfaff) ||
        (c >= 0x20000 && c <= 0x2ffff) || c == 0x3005) return Han;
    if ((c >= 0x370 && c <= 0x3ff) || (c >= 0x400 && c <= 0x52f) || (c >= 0x590 && c <= 0x6ff) ||
        (c >= 0xe00 && c <= 0xe7f)) return OtherLetters;
    return 0; // spaces, digits, punctuation, symbols
}
bool isSpace(unsigned char c) { return c <= 0x20; }
}
unsigned nativeScripts(const std::string& language) {
    if (language == "ko") return Hangul;
    if (language == "en") return Latin;
    if (language == "ja") return Kana | Han;
    if (language == "zh") return Han;
    return 0;
}
std::vector<TextSegment> splitForTranslation(const std::string& text, unsigned native) {
    std::vector<TextSegment> result;
    if (text.empty()) return result;
    if (native == 0) { result.push_back({text, true}); return result; }
    // 1. Tokens: runs of native letters, foreign letters, or neutral characters.
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t begin = i;
        const char32_t cp = decode(text, i);
        const unsigned script = scriptOf(cp);
        const Kind kind = script == 0 ? Kind::Neutral : (script & native) ? Kind::Native : Kind::Foreign;
        const bool capital = (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9');
        if (!tokens.empty() && tokens.back().kind == kind && tokens.back().end == begin) {
            tokens.back().end = i;
            if (kind != Kind::Neutral) ++tokens.back().letters;
            tokens.back().acronym = tokens.back().acronym && capital;
        } else {
            tokens.push_back({begin, i, kind, kind == Kind::Neutral ? 0 : 1, kind != Kind::Neutral && capital});
        }
    }
    // 2. Smooth OCR noise and short fragments by looking at the neighbouring letter runs.
    auto neighbour = [&](std::size_t index, int step) -> const Token* {
        for (auto k = static_cast<std::ptrdiff_t>(index) + step; k >= 0 && k < static_cast<std::ptrdiff_t>(tokens.size()); k += step)
            if (tokens[k].kind != Kind::Neutral) return &tokens[k];
        return nullptr;
    };
    for (std::size_t k = 0; k < tokens.size(); ++k) {
        Token& t = tokens[k];
        if (t.kind == Kind::Neutral) continue;
        const Token* before = neighbour(k, -1);
        const Token* after = neighbour(k, +1);
        if (t.letters == 1) {
            // A lone character takes the kind of its surroundings (usually a misread glyph).
            if (before && after && before->kind == after->kind) t.kind = before->kind;
            else if (before && !after) t.kind = before->kind;
            else if (after && !before) t.kind = after->kind;
        } else if (t.kind == Kind::Native && t.letters <= 2 && before && after &&
                   before->kind == Kind::Foreign && after->kind == Kind::Foreign) {
            t.kind = Kind::Foreign; // keeps a foreign sentence in one piece
        } else if (t.kind == Kind::Foreign && t.acronym && t.letters <= 4 &&
                   ((before && before->kind == Kind::Native) || (after && after->kind == Kind::Native))) {
            t.kind = Kind::Native; // "HP", "NPC" inside a native sentence stay as they are
        }
    }
    // 3. Segments: neutral text between runs of the same kind joins them; closing
    //    punctuation right after a foreign run travels with it ("Hello!" -> "안녕하세요!").
    auto push = [&](std::size_t begin, std::size_t end, bool translate) {
        if (begin >= end) return;
        if (!result.empty() && result.back().translate == translate) result.back().utf8 += text.substr(begin, end - begin);
        else result.push_back({text.substr(begin, end - begin), translate});
    };
    std::size_t k = 0;
    while (k < tokens.size()) {
        if (tokens[k].kind == Kind::Neutral) { push(tokens[k].begin, tokens[k].end, false); ++k; continue; }
        const Kind kind = tokens[k].kind;
        const std::size_t begin = tokens[k].begin;
        std::size_t end = tokens[k].end;
        std::size_t next = k + 1;
        while (next < tokens.size()) {
            if (tokens[next].kind == kind) { end = tokens[next].end; ++next; continue; }
            if (tokens[next].kind == Kind::Neutral && next + 1 < tokens.size() && tokens[next + 1].kind == kind) {
                end = tokens[next + 1].end; next += 2; continue;
            }
            break;
        }
        if (kind == Kind::Foreign && next < tokens.size() && tokens[next].kind == Kind::Neutral) {
            std::size_t cut = tokens[next].begin;
            while (cut < tokens[next].end && !isSpace(static_cast<unsigned char>(text[cut]))) ++cut;
            push(begin, cut, true);
            push(cut, tokens[next].end, false);
            k = next + 1;
            continue;
        }
        push(begin, end, kind == Kind::Foreign);
        k = next;
    }
    return result;
}
unsigned scriptsIn(const std::string& text) {
    unsigned scripts = 0;
    for (std::size_t i = 0; i < text.size();) scripts |= scriptOf(decode(text, i));
    return scripts;
}
std::string joinTranslated(const std::vector<TextSegment>& segments, const std::vector<std::string>& translations) {
    std::string out;
    std::size_t used = 0;
    for (const auto& segment : segments) {
        if (!segment.translate) { out += segment.utf8; continue; }
        if (used >= translations.size()) return {};
        out += translations[used++];
    }
    return used == translations.size() ? out : std::string();
}
}
