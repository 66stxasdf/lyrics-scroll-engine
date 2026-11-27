// jni/native-lib.cpp
// 可选：Android JNI 桥接层，把 C++ 歌词引擎暴露给 Java 层。
// 这是"引擎如何在真实 App 中被调用"的范例，不属于纯 C++ 核心；
// 不参与 PC 端的编译/测试（需要 Android NDK 与 <jni.h> / <android/log.h>）。
//
// 真实工程见原项目 SodaLyrics：app/src/main/cpp/native-lib.cpp
#include <jni.h>
#include <string>
#include <vector>

#include "lyrics_engine.h"  // 注意：在 App 工程中此头文件位于 ../../cpp/

namespace {
lyrics::Engine& engine() {
    static lyrics::Engine e;
    return e;
}
std::string jstrToStd(JNIEnv* env, jstring s) {
    if (!s) return std::string();
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    env->ReleaseStringUTFChars(s, c);
    return r;
}
}  // namespace

extern "C" {

JNIEXPORT jint JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeLoadLrc(JNIEnv* env, jobject, jstring lrc) {
    return engine().loadLrc(jstrToStd(env, lrc));
}
JNIEXPORT jint JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeLoadPlain(JNIEnv* env, jobject,
                                                     jobjectArray lines, jlong dur) {
    std::vector<std::string> v;
    if (lines) {
        const jsize n = env->GetArrayLength(lines);
        v.reserve(static_cast<size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            jstring js = static_cast<jstring>(env->GetObjectArrayElement(lines, i));
            v.push_back(jstrToStd(env, js));
            if (js) env->DeleteLocalRef(js);
        }
    }
    return engine().loadPlainLines(v, static_cast<int64_t>(dur));
}
JNIEXPORT void JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeSetOffset(JNIEnv*, jobject, jlong ms) {
    engine().setOffsetMs(static_cast<int64_t>(ms));
}
JNIEXPORT void JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeUpdatePosition(JNIEnv*, jobject, jlong pos) {
    engine().updatePosition(static_cast<int64_t>(pos));
}
JNIEXPORT jstring JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeCurrentText(JNIEnv* env, jobject) {
    return env->NewStringUTF(engine().currentText().c_str());
}
JNIEXPORT jstring JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativePrevText(JNIEnv* env, jobject) {
    return env->NewStringUTF(engine().prevText().c_str());
}
JNIEXPORT jstring JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeNextText(JNIEnv* env, jobject) {
    return env->NewStringUTF(engine().nextText().c_str());
}
JNIEXPORT jint JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeHighlightedChars(JNIEnv*, jobject) {
    return engine().highlightedChars();
}
JNIEXPORT jfloat JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeScrollProgress(JNIEnv*, jobject) {
    return engine().scrollProgress();
}
JNIEXPORT jlong JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeCurrentIndex(JNIEnv*, jobject) {
    return engine().currentIndex();
}
JNIEXPORT jint JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeLineCount(JNIEnv*, jobject) {
    return engine().lineCount();
}
JNIEXPORT void JNICALL
Java_com_soda_lyricsoff_LyricsEngine_nativeClear(JNIEnv*, jobject) {
    engine().clear();
}

}  // extern "C"
