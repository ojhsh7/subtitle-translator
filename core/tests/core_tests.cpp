#include "subtitle/core.h"
#include "subtitle/models.h"
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
    std::cout << "shared core: all checks passed\n";
}
