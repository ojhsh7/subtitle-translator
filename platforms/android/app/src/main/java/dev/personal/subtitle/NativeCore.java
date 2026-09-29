package dev.personal.subtitle;
final class NativeCore {
    static { System.loadLibrary("subtitle_core_jni"); }
    static native long create();
    static native void destroy(long p);
    static native long begin(long p,int source,int w,int h);
    static native int accept(long p,long id,byte[] text);
    static native boolean complete(long p,long id,byte[] text);
    static native byte[] source(long p);
    static native byte[] result(long p);
    static native void cancel(long p);
    static native void fail(long p,long id);
    static native void ocrOnly(long p,long id);
    static native void resetCache(long p);
}
