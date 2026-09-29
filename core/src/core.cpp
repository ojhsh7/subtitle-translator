#include "subtitle/core.h"
#include "subtitle/models.h"
#include <string>
#include <new>
namespace subtitle {
std::string normalizeText(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool separator = false;
    for (std::size_t i = 0; i < input.size();) {
        const auto c = static_cast<unsigned char>(input[i]);
        std::size_t whitespace = 0;
        if (c <= 0x20 || c == 0x7f) whitespace = 1;
        else if (i + 1 < input.size() && c == 0xc2 &&
                 static_cast<unsigned char>(input[i + 1]) == 0xa0) whitespace = 2;
        else if (i + 2 < input.size() && c == 0xe3 &&
                 static_cast<unsigned char>(input[i + 1]) == 0x80 &&
                 static_cast<unsigned char>(input[i + 2]) == 0x80) whitespace = 3;
        if (whitespace) { separator = !out.empty(); i += whitespace; continue; }
        if (separator) { out.push_back(' '); separator = false; }
        out.push_back(input[i++]);
    }
    return out;
}
}
struct st_session {
    uint64_t sequence = 0;
    int state = ST_IDLE;
    subtitle::ImageInput image{subtitle::InputSource::ImageFile, 0, 0};
    subtitle::OcrResult ocr;
    std::string translated, cachedSource, cachedTranslation;
};
extern "C" {
st_session* st_create(void) { return new (std::nothrow) st_session; }
void st_destroy(st_session* s) { delete s; }
uint64_t st_begin(st_session* s, int source, int width, int height) {
    if (!s || s->state == ST_OCR || s->state == ST_TRANSLATING ||
        source < 0 || source > 2 || width <= 0 || height <= 0 ||
        static_cast<int64_t>(width) * height > 16000000) return 0;
    s->image = {static_cast<subtitle::InputSource>(source), width, height};
    s->ocr = {}; s->translated.clear(); s->state = ST_OCR;
    return ++s->sequence;
}
int st_accept_ocr(st_session* s, uint64_t request, const char* text) {
    if (!s || request != s->sequence || s->state != ST_OCR) return ST_STALE;
    if (!text || std::char_traits<char>::length(text) > 32768) {
        s->state = ST_FAILED; return text ? ST_TOO_LONG : ST_EMPTY;
    }
    s->ocr.utf8Text = subtitle::normalizeText(text);
    if (s->ocr.utf8Text.empty()) { s->state = ST_FAILED; return ST_EMPTY; }
    if (s->ocr.utf8Text == s->cachedSource && !s->cachedTranslation.empty()) {
        s->translated = s->cachedTranslation; s->state = ST_COMPLETE; return ST_CACHED;
    }
    s->state = ST_TRANSLATING;
    return ST_TRANSLATE;
}
int st_complete(st_session* s, uint64_t request, const char* translated) {
    if (!s || request != s->sequence || s->state != ST_TRANSLATING ||
        !translated || !*translated) return 0;
    if (std::char_traits<char>::length(translated) > 131072) { s->state = ST_FAILED; return 0; }
    s->translated = translated;
    s->cachedSource = s->ocr.utf8Text; s->cachedTranslation = s->translated;
    s->state = ST_COMPLETE; return 1;
}
void st_finish_ocr(st_session* s, uint64_t request) {
    if (s && request == s->sequence && s->state == ST_TRANSLATING) s->state = ST_COMPLETE;
}
void st_fail(st_session* s, uint64_t request) {
    if (s && request == s->sequence && (s->state == ST_OCR || s->state == ST_TRANSLATING))
        s->state = ST_FAILED;
}
void st_cancel(st_session* s) {
    if (!s) return;
    ++s->sequence;
    s->state = ST_CANCELLED; s->ocr = {}; s->translated.clear();
}
int st_state(const st_session* s) { return s ? s->state : ST_FAILED; }
const char* st_source_text(const st_session* s) { return s ? s->ocr.utf8Text.c_str() : ""; }
const char* st_translation(const st_session* s) { return s ? s->translated.c_str() : ""; }
void st_reset_cache(st_session* s) { if (s) { s->cachedSource.clear(); s->cachedTranslation.clear(); } }
}
