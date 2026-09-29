#include <jni.h>
#include "subtitle/core.h"
#include <string>
static st_session* session(jlong p) { return reinterpret_cast<st_session*>(p); }
// Explicit UTF-8 byte arrays avoid JNI modified UTF-8 differences.
static std::string bytes(JNIEnv* e,jbyteArray a) {
    auto n=e->GetArrayLength(a); std::string s(n,'\0');
    e->GetByteArrayRegion(a,0,n,reinterpret_cast<jbyte*>(s.data())); return s;
}
static jbyteArray array(JNIEnv* e,const char* p) {
    std::string s(p); auto a=e->NewByteArray(static_cast<jsize>(s.size()));
    e->SetByteArrayRegion(a,0,static_cast<jsize>(s.size()),reinterpret_cast<const jbyte*>(s.data())); return a;
}
extern "C" JNIEXPORT jlong JNICALL Java_dev_personal_subtitle_NativeCore_create(JNIEnv*,jclass) { return reinterpret_cast<jlong>(st_create()); }
extern "C" JNIEXPORT void JNICALL Java_dev_personal_subtitle_NativeCore_destroy(JNIEnv*,jclass,jlong p) { st_destroy(session(p)); }
extern "C" JNIEXPORT jlong JNICALL Java_dev_personal_subtitle_NativeCore_begin(JNIEnv*,jclass,jlong p,jint source,jint w,jint h) { return st_begin(session(p),source,w,h); }
extern "C" JNIEXPORT jint JNICALL Java_dev_personal_subtitle_NativeCore_accept(JNIEnv* e,jclass,jlong p,jlong id,jbyteArray text) { return st_accept_ocr(session(p),id,bytes(e,text).c_str()); }
extern "C" JNIEXPORT jboolean JNICALL Java_dev_personal_subtitle_NativeCore_complete(JNIEnv* e,jclass,jlong p,jlong id,jbyteArray text) { return st_complete(session(p),id,bytes(e,text).c_str()) != 0; }
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_personal_subtitle_NativeCore_source(JNIEnv* e,jclass,jlong p) { return array(e,st_source_text(session(p))); }
extern "C" JNIEXPORT jbyteArray JNICALL Java_dev_personal_subtitle_NativeCore_result(JNIEnv* e,jclass,jlong p) { return array(e,st_translation(session(p))); }
extern "C" JNIEXPORT void JNICALL Java_dev_personal_subtitle_NativeCore_cancel(JNIEnv*,jclass,jlong p) { st_cancel(session(p)); }
extern "C" JNIEXPORT void JNICALL Java_dev_personal_subtitle_NativeCore_fail(JNIEnv*,jclass,jlong p,jlong id) { st_fail(session(p),id); }
extern "C" JNIEXPORT void JNICALL Java_dev_personal_subtitle_NativeCore_ocrOnly(JNIEnv*,jclass,jlong p,jlong id) { st_finish_ocr(session(p),id); }
extern "C" JNIEXPORT void JNICALL Java_dev_personal_subtitle_NativeCore_resetCache(JNIEnv*,jclass,jlong p) { st_reset_cache(session(p)); }
