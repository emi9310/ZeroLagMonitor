#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <atomic>

#include "MediaCodecDecoder.cpp"
#include "Receiver.hpp"

#define LOG_TAG "MyrrorJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
    ANativeWindow*           g_window    = nullptr;
    MediaCodecDecoder*       g_decoder   = nullptr;
    Receiver*                g_receiver  = nullptr;

    // Para el callback al primer frame
    JavaVM*                  g_jvm       = nullptr;
    jobject                  g_activity  = nullptr;
    std::atomic<bool>        g_first_frame_sent{false};
}

// Llama a un método void de la Activity desde cualquier hilo
static void NotifyActivity(const char* method_name) {
    if (!g_jvm || !g_activity) return;
    JNIEnv* env = nullptr;
    bool attached = false;

    jint status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return;

    jclass    cls = env->GetObjectClass(g_activity);
    jmethodID mid = env->GetMethodID(cls, method_name, "()V");
    if (mid) env->CallVoidMethod(g_activity, mid);
    env->DeleteLocalRef(cls);

    if (attached) g_jvm->DetachCurrentThread();
}

// Llama a MainActivity.onFirstFrame() desde cualquier hilo
static void NotifyFirstFrame() { NotifyActivity("onFirstFrame"); }

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_zerolag_monitor_MainActivity_nativeStartStream(
        JNIEnv*  env,
        jobject  thiz,
        jobject  surface,
        jint     width,
        jint     height,
        jboolean use_hevc,
        jint     port)
{
    // Guardar referencias para el callback
    env->GetJavaVM(&g_jvm);
    g_activity = env->NewGlobalRef(thiz);
    g_first_frame_sent = false;

    g_window = ANativeWindow_fromSurface(env, surface);
    if (!g_window) {
        LOGE("ANativeWindow_fromSurface devolvió null");
        return JNI_FALSE;
    }

    g_decoder = new MediaCodecDecoder(
        g_window,
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<bool>(use_hevc)
    );

    if (!g_decoder->Init()) {
        LOGE("MediaCodecDecoder::Init() falló");
        delete g_decoder; g_decoder = nullptr;
        ANativeWindow_release(g_window); g_window = nullptr;
        return JNI_FALSE;
    }

    g_receiver = new Receiver(static_cast<int>(port));

    bool ok = g_receiver->Start(
        // Callback de datos — cada NAL recibido
        [](const uint8_t* data, size_t size, uint64_t pts_us) {
            if (g_decoder) {
                g_decoder->FeedNAL(data, size, static_cast<int64_t>(pts_us));
            }
            // Primer frame → ocultar overlay en la UI
            if (!g_first_frame_sent.exchange(true)) {
                NotifyFirstFrame();
            }
        },
        // Callback de desconexión — PC cortó el stream
        []() {
            g_first_frame_sent = false;  // resetear para la próxima conexión
            NotifyActivity("onStreamStopped");
        }
    );

    if (!ok) {
        LOGE("Receiver::Start() falló");
        delete g_receiver; g_receiver = nullptr;
        delete g_decoder;  g_decoder  = nullptr;
        ANativeWindow_release(g_window); g_window = nullptr;
        return JNI_FALSE;
    }

    LOGI("Stream iniciado — %dx%d %s puerto %d",
         width, height, use_hevc ? "H.265" : "H.264", port);
    return JNI_TRUE;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_zerolag_monitor_MainActivity_nativeStopStream(
        JNIEnv* env,
        jobject /* thiz */)
{
    if (g_receiver) { g_receiver->Stop(); delete g_receiver; g_receiver = nullptr; }
    if (g_decoder)  { g_decoder->Stop();  delete g_decoder;  g_decoder  = nullptr; }
    if (g_window)   { ANativeWindow_release(g_window); g_window = nullptr; }

    // Liberar referencia global a la Activity
    if (g_activity) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }
    g_first_frame_sent = false;

    LOGI("Stream detenido.");
}
