#include "subtitle/core.h"
#include "subtitle/models.h"
#include "subtitle/text.h"
#include <iostream>
#include <cstdlib>
#include <string>
void check(bool value, const char* description) {
    if (!value) { std::cerr << description << '\n'; std::exit(1); }
}
int main() {
    check(subtitle::normalizeText(" \r\nこんにちは　世界\t ") == "こんにちは 世界", "UTF8 whitespace cleanup");
    auto* s = st_create(); check(s != nullptr, "session allocation");
    check(st_begin(s, 0, 0, 100) == 0, "reject invalid dimensions");
    check(st_begin(s, 0, 100000, 100000) == 0, "reject huge image");
    const auto a = st_begin(s, ST_SHARED_IMAGE, 1280, 720);
    check(a != 0 && st_state(s) == ST_OCR, "begin OCR");
    check(st_begin(s, 0, 20, 20) == 0, "ignore duplicate while busy");
    st_cancel(s);
    check(st_accept_ocr(s, a, "stale") == ST_STALE, "ignore cancelled OCR");
    const auto b = st_begin(s, ST_SCREEN_CAPTURE, 100, 80);
    check(st_accept_ocr(s, b, "　\n ") == ST_EMPTY, "empty OCR");
    const auto c = st_begin(s, ST_IMAGE_FILE, 100, 80);
    check(st_accept_ocr(s, c, "こんにちは") == ST_TRANSLATE, "translation needed");
    check(!st_complete(s, b, "old response"), "ignore old response");
    check(st_complete(s, c, "안녕하세요") == 1, "complete translation");
    const auto d = st_begin(s, ST_IMAGE_FILE, 100, 80);
    check(st_accept_ocr(s, d, " こんにちは\n") == ST_CACHED, "last successful text deduplication");
    check(std::string(st_translation(s)) == "안녕하세요", "cached value");
    st_reset_cache(s);
    const auto e = st_begin(s, ST_IMAGE_FILE, 100, 80);
    check(st_accept_ocr(s, e, "こんにちは") == ST_TRANSLATE, "settings change invalidates cache");
    st_fail(s, e);
    const auto f = st_begin(s, ST_IMAGE_FILE, 100, 80);
    const std::string huge(32769, 'x');
    check(st_accept_ocr(s, f, huge.c_str()) == ST_TOO_LONG, "bound text size");
    st_destroy(s);

    using subtitle::splitForTranslation; using subtitle::joinTranslated;
    const unsigned korean = subtitle::nativeScripts("ko");
    auto pieces = [](const std::vector<subtitle::TextSegment>& segments) {
        std::string out;
        for (const auto& segment : segments) out += (segment.translate ? "[T:" : "[K:") + segment.utf8 + "]";
        return out;
    };
    check(pieces(splitForTranslation("으응, 제 타액과 선배님의 타액을 모아서", korean)) == "[K:으응, 제 타액과 선배님의 타액을 모아서]",
          "Korean text is kept as is");
    check(pieces(splitForTranslation("今日はいい天気です。", korean)) == "[T:今日はいい天気です。]", "Japanese sentence in one piece");
    check(pieces(splitForTranslation("Press any key to continue.", korean)) == "[T:Press any key to continue.]", "English sentence");
    check(pieces(splitForTranslation("채주아: Hello, how are you?", korean)) == "[K:채주아: ][T:Hello, how are you?]",
          "only the foreign part of a mixed line");
    check(pieces(splitForTranslation("Hello! 반가워요", korean)) == "[T:Hello!][K: 반가워요]", "closing punctuation travels");
    check(pieces(splitForTranslation("HP가 회복되었습니다", korean)) == "[K:HP가 회복되었습니다]", "acronyms stay");
    check(pieces(splitForTranslation("今日は이いい天気", korean)) == "[T:今日は이いい天気]", "a misread glyph does not split a sentence");
    check(pieces(splitForTranslation("선배l의 타액", korean)) == "[K:선배l의 타액]", "a stray Latin letter in Korean is noise");
    check(pieces(splitForTranslation("Press any key", subtitle::nativeScripts("en"))) == "[K:Press any key]", "English target keeps English");
    check(pieces(splitForTranslation("セーブしますか", subtitle::nativeScripts("ja"))) == "[K:セーブしますか]", "Japanese target keeps kana and kanji");
    check(pieces(splitForTranslation("abc", 0)) == "[T:abc]", "unknown target translates everything");
    check(splitForTranslation("", korean).empty(), "empty text");
    check(pieces(splitForTranslation("\xff\xfe 안녕", korean)) == "[K:\xff\xfe 안녕]", "invalid UTF-8 is kept, not translated");
    const auto mixed = splitForTranslation("채주아: Hello, how are you?", korean);
    check(joinTranslated(mixed, {"안녕, 잘 지내?"}) == "채주아: 안녕, 잘 지내?", "rebuild mixed line");
    check(joinTranslated(mixed, {}).empty() && joinTranslated(mixed, {"a", "b"}).empty(), "translation count must match");
    check(subtitle::scriptsIn("See you.") == subtitle::Latin, "Latin letters only");
    check(subtitle::scriptsIn("今日はいい天気") == (subtitle::Kana | subtitle::Han), "kana and kanji");
    check(subtitle::scriptsIn("HP 100 회복") == (subtitle::Latin | subtitle::Hangul), "digits are ignored");
    check(subtitle::scriptsIn("123 !?") == 0, "no letters");
    check(subtitle::scriptsIn("Tôi đã bảo bạn đừng mở cánh cửa đó.") == subtitle::Latin, "Vietnamese is Latin");
    check(subtitle::scriptsIn("Сохранить прогресс?") == subtitle::Cyrillic, "Cyrillic");
    check(subtitle::scriptsIn("บันทึกเกม") == subtitle::Thai, "Thai with its combining marks");
    check(subtitle::scriptsIn("هل تريد حفظ اللعبة؟") == subtitle::Arabic, "Arabic");
    check(subtitle::scriptsIn("क्या आप गेम सेव करना चाहते हैं?") == subtitle::Devanagari, "Devanagari with danda-free text");
    check(pieces(splitForTranslation("Tôi đã bảo bạn", korean)) == "[T:Tôi đã bảo bạn]", "Vietnamese stays one piece");
    check(pieces(splitForTranslation("채주아: Сохранить?", korean)) == "[K:채주아: ][T:Сохранить?]", "Cyrillic is foreign for Korean");
    using subtitle::preservableScripts;
    check(pieces(splitForTranslation("Bonjour le monde.", preservableScripts("en", "fr"))) == "[T:Bonjour le monde.]",
          "French must be translated even for an English target");
    check(pieces(splitForTranslation("Möchtest du speichern?", preservableScripts("en", ""))) == "[T:Möchtest du speichern?]",
          "Unknown Latin text must not be labelled English");
    check(pieces(splitForTranslation("Press any key", preservableScripts("en", "en"))) == "[K:Press any key]",
          "Explicit English source and target may skip the request");
    check(pieces(splitForTranslation("保存", preservableScripts("ja", "zh"))) == "[T:保存]",
          "Han text must not be silently treated as Japanese");
    check(preservableScripts("ko", "") == subtitle::Hangul, "Korean mixed-script preservation still works");
    check(pieces(splitForTranslation("今日はいい天気です。", preservableScripts("ja", ""))) == "[K:今日はいい天気です。]",
          "a Japanese line keeps its kanji when translating into Japanese");
    check(pieces(splitForTranslation("今天天气很好。", preservableScripts("ja", ""))) == "[T:今天天气很好。]",
          "a kanji-only line may be Chinese and is still translated");
    check(pieces(splitForTranslation("今日は晴れです。\n今天天气很好。", preservableScripts("ja", ""))) ==
          "[K:今日は晴れです。\n][T:今天天气很好。]",
          "Japanese on one line must not suppress translation of a Chinese line");
    check(joinTranslated(splitForTranslation("今天天气很好。\r\n今日は晴れです。", preservableScripts("ja", "")),
          {"今日は天気がいいです。"}) == "今日は天気がいいです。\r\n今日は晴れです。",
          "mixed-line translation preserves original CRLF separators");
    // B-04: OCR readings of symbol-only screens that QA found still sent (11 of 72 conditions).
    using subtitle::worthTranslating;
    for (const char* noise : {"→←↓⇒L", "[jrJ[]", "[]r][]", "ー・ー |", "v☆XO△", "×O", "O", "★ ▶ ※", "Lv.10", "HP 30/50", "", " \n"})
        check(!worthTranslating(noise), noise);
    for (const char* real : {"Press any key to continue.", "今日はいい天気です。", "はい", "「うん」", "雨", "Сохранить?",
                             "Save game?", "Tôi đã"})
        check(worthTranslating(real), real);
    auto line = splitForTranslation("Lv.10 달성!", korean);
    check(subtitle::keepUnworthySegments(line) == 1 && pieces(line) == "[K:Lv.10 달성!]", "game stat token stays and merges");
    line = splitForTranslation("★ Press any key", korean);
    check(subtitle::keepUnworthySegments(line) == 0 && pieces(line) == "[K:★ ][T:Press any key]", "a real sentence is still sent");
    std::cout << "shared core: all checks passed\n";
}
