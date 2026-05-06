#include "AMFEncoder.hpp"
#include <cstdio>

// AMF se carga en tiempo de ejecución para no fallar en máquinas sin AMD
typedef AMF_RESULT(AMF_CDECL_CALL* PFN_AMFQueryVersion)(amf_uint64*);
typedef AMF_RESULT(AMF_CDECL_CALL* PFN_AMFInit)(amf_uint64, amf::AMFFactory**);

bool AMFEncoder::Init(ID3D11Device* device, const EncoderConfig& cfg) {
    device_ = device;
    cfg_    = cfg;

    // ── Cargar amfrt64.dll en runtime ───────────────────────────
    amf_dll_ = LoadLibraryA("amfrt64.dll");
    if (!amf_dll_) {
        std::fprintf(stderr, "[AMF] amfrt64.dll no encontrada. ¿Tienes drivers AMD?\n");
        return false;
    }

    auto AMFInit = reinterpret_cast<PFN_AMFInit>(
        GetProcAddress(amf_dll_, AMF_INIT_FUNCTION_NAME));
    if (!AMFInit) {
        std::fprintf(stderr, "[AMF] Símbolo AMFInit no encontrado.\n");
        return false;
    }

    AMF_RESULT res = AMFInit(AMF_FULL_VERSION, &factory_);
    if (res != AMF_OK) {
        std::fprintf(stderr, "[AMF] AMFInit falló: %d\n", res);
        return false;
    }

    // ── Crear contexto D3D11 (misma GPU que DXGI capture) ───────
    factory_->CreateContext(&context_);
    res = context_->InitDX11(device_);
    if (res != AMF_OK) {
        std::fprintf(stderr, "[AMF] InitDX11 falló: %d\n", res);
        return false;
    }

    // ── Crear componente encoder HEVC o H264 ────────────────────
    const wchar_t* codec = cfg_.use_hevc
        ? AMFVideoEncoder_HEVC
        : AMFVideoEncoderVCE_AVC;

    res = factory_->CreateComponent(context_, codec, &encoder_);
    if (res != AMF_OK) {
        std::fprintf(stderr, "[AMF] CreateComponent %ls falló: %d\n", codec, res);
        return false;
    }

    // ── Parámetros de baja latencia ──────────────────────────────
    if (cfg_.use_hevc) {
        if (!ConfigureHEVC()) return false;
    } else {
        if (!ConfigureH264()) return false;
    }

    res = encoder_->Init(amf::AMF_SURFACE_BGRA, cfg_.width, cfg_.height);
    if (res != AMF_OK) {
        std::fprintf(stderr, "[AMF] encoder->Init falló: %d\n", res);
        return false;
    }

    ready_ = true;
    std::printf("[AMF] Listo — %ux%u @ %u fps, %u Mbps, %ls\n",
                cfg_.width, cfg_.height, cfg_.fps,
                cfg_.bitrate / 1'000'000, codec);
    return true;
}

bool AMFEncoder::ConfigureHEVC() {
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE,
                          AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET,
                          AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE,
                          static_cast<amf_int64>(cfg_.bitrate));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE,
                          ::AMFConstructSize(cfg_.width, cfg_.height));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE,
                          ::AMFConstructRate(cfg_.fps, 1));
    // Sin B-frames, sin buffering extra
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_NUM_GOPS_PER_IDR,       0);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_MAX_NUM_REFRAMES,        1);
    return true;
}

bool AMFEncoder::ConfigureH264() {
    encoder_->SetProperty(AMF_VIDEO_ENCODER_USAGE,
                          AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET,
                          AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE,
                          static_cast<amf_int64>(cfg_.bitrate));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE,
                          ::AMFConstructSize(cfg_.width, cfg_.height));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE,
                          ::AMFConstructRate(cfg_.fps, 1));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0); // sin B-frames
    encoder_->SetProperty(AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, 1);
    return true;
}

bool AMFEncoder::EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                              const PacketCallback& cb) {
    if (!ready_) return false;

    // ► Envolver la textura D3D11 como AMFSurface — zero-copy
    amf::AMFSurfacePtr surface;
    AMF_RESULT res = context_->CreateSurfaceFromDX11Native(
        tex, &surface, nullptr);
    if (res != AMF_OK) return false;

    surface->SetPts(static_cast<amf_pts>(pts_us) * 10); // AMF usa 100ns units

    // ► Enviar al encoder (async internamente)
    res = encoder_->SubmitInput(surface);
    if (res != AMF_OK && res != AMF_NEED_MORE_INPUT) return false;

    // ► Recoger el NAL unit de salida (puede requerir varios QueryOutput)
    amf::AMFDataPtr data;
    do {
        res = encoder_->QueryOutput(&data);
    } while (res == AMF_REPEAT);

    if (res == AMF_OK && data) {
        amf::AMFBufferPtr buf(data);
        cb(static_cast<const uint8_t*>(buf->GetNative()),
           buf->GetSize(),
           pts_us);
    }

    return true;
}

void AMFEncoder::Shutdown() {
    if (encoder_) {
        encoder_->Drain();
        encoder_->Flush();
        encoder_ = nullptr;
    }
    context_ = nullptr;
    if (amf_dll_) {
        FreeLibrary(amf_dll_);
        amf_dll_ = nullptr;
    }
    ready_ = false;
}
