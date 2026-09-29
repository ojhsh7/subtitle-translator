#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct st_session st_session;
enum st_input_source { ST_IMAGE_FILE = 0, ST_SHARED_IMAGE = 1, ST_SCREEN_CAPTURE = 2 };
enum st_request_state { ST_IDLE = 0, ST_OCR = 1, ST_TRANSLATING = 2, ST_COMPLETE = 3, ST_FAILED = 4, ST_CANCELLED = 5 };
enum st_ocr_decision { ST_STALE = 0, ST_TRANSLATE = 1, ST_CACHED = 2, ST_EMPTY = 3, ST_TOO_LONG = 4 };
st_session* st_create(void);
void st_destroy(st_session* session);
/* Single owner thread. Returns 0 for invalid image metadata or while busy. */
uint64_t st_begin(st_session* session, int source, int width, int height);
/* UTF-8 input. Only the current request can transition state. */
int st_accept_ocr(st_session* session, uint64_t request, const char* utf8_text);
int st_complete(st_session* session, uint64_t request, const char* utf8_translation);
void st_finish_ocr(st_session* session, uint64_t request);
void st_fail(st_session* session, uint64_t request);
void st_cancel(st_session* session);
int st_state(const st_session* session);
/* Pointers remain valid until the next mutation of session. Copy in native adapters. */
const char* st_source_text(const st_session* session);
const char* st_translation(const st_session* session);
void st_reset_cache(st_session* session);
#ifdef __cplusplus
}
#endif
