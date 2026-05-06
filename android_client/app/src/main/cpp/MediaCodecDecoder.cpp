#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <thread>
#include <atomic>
#include <cstring>

class MediaCodecDecoder {
public:
    MediaCodecDecoder(ANativeWindow* window, int width, int height, bool hevc)
        : window_(window), width_(width), height_(height), hevc_(hevc) {}

    bool Init() {
        const char* mime = hevc_ ? "video/hevc" : "video/avc";
        codec_ = AMediaCodec_createDecoderByType(mime);

        AMediaFormat* fmt = AMediaFormat_new();
        AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME,   mime);
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_WIDTH,  width_);
        AMediaFormat_setInt32 (fmt, AMEDIAFORMAT_KEY_HEIGHT, height_);
        // Low-latency: sin buffering extra de frames
        AMediaFormat_setInt32 (fmt, "low-latency", 1);

        // ► Surface output: los frames decodificados van DIRECTO a la GPU
        //   sin pasar por CPU (zero-copy decode → display)
        AMediaCodec_configure(codec_, fmt, window_, nullptr,
                              0 /* flags: decoder */);
        AMediaFormat_delete(fmt);

        AMediaCodec_start(codec_);
        output_thread_ = std::thread(&MediaCodecDecoder::OutputLoop, this);
        return true;
    }

    // Llamado desde el hilo de red con cada NAL unit recibida
    void FeedNAL(const uint8_t* data, size_t size, int64_t pts_us) {
        ssize_t idx = AMediaCodec_dequeueInputBuffer(codec_, 2000 /*us timeout*/);
        if (idx < 0) return; // ocupado, descartamos (mejor que bloquear)

        size_t buf_size;
        uint8_t* buf = AMediaCodec_getInputBuffer(codec_, idx, &buf_size);
        size_t   copy = std::min(size, buf_size);
        std::memcpy(buf, data, copy);

        AMediaCodec_queueInputBuffer(codec_, idx, 0, copy, pts_us, 0);
    }

    void Stop() {
        running_ = false;
        output_thread_.join();
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
    }

private:
    // Hilo dedicado a desencolar frames decodificados
    // Los entrega directo al Surface → no hay copia CPU-side
    void OutputLoop() {
        AMediaCodecBufferInfo info{};
        while (running_) {
            ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, 3000);
            if (idx >= 0) {
                // render=true → el frame va al Surface (GPU) sin CPU touch
                AMediaCodec_releaseOutputBuffer(codec_, idx, /*render=*/true);
            }
        }
    }

    AMediaCodec*     codec_  = nullptr;
    ANativeWindow*   window_ = nullptr;
    int              width_, height_;
    bool             hevc_;
    std::atomic<bool> running_{true};
    std::thread      output_thread_;
};
