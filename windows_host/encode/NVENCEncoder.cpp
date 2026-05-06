#include "NVENCEncoder.hpp"
#include <cstring>
#include <cstdio>

bool NVENCEncoder::Init(ID3D11Device* device, const EncoderConfig& cfg) {
    device_ = device;
    cfg_    = cfg;

    // ── Cargar la API de NVENC ──────────────────────────────────
    fn_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (NvEncodeAPICreateInstance(&fn_) != NV_ENC_SUCCESS) {
        std::fprintf(stderr, "[NVENC] nvEncodeAPI.dll no encontrada o GPU no compatible.\n");
        return false;
    }

    // ── Abrir sesión D3D11 ───────────────────────────────────────
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params{};
    params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    params.device     = device_;
    params.apiVersion = NVENCAPI_VERSION;

    NVENCSTATUS st = fn_.nvEncOpenEncodeSessionEx(&params, &nvenc_session_);
    if (st != NV_ENC_SUCCESS) {
        std::fprintf(stderr, "[NVENC] No se pudo abrir sesión: %d\n", st);
        return false;
    }

    // ── Configurar encoder: preset LOW_LATENCY, sin B-frames ────
    NV_ENC_CONFIG enc_cfg{};
    enc_cfg.version = NV_ENC_CONFIG_VER;
    enc_cfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    enc_cfg.rcParams.averageBitRate  = cfg_.bitrate;
    // Sin B-frames = mínima latencia end-to-end
    if (cfg_.use_hevc)
        enc_cfg.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB = 1;
    else
        enc_cfg.encodeCodecConfig.h264Config.maxNumRefFrames = 1;

    NV_ENC_INITIALIZE_PARAMS init{};
    init.version           = NV_ENC_INITIALIZE_PARAMS_VER;
    init.encodeGUID        = cfg_.use_hevc ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    init.presetGUID        = NV_ENC_PRESET_P3_GUID; // LOW_LATENCY_HQ
    init.encodeWidth       = cfg_.width;
    init.encodeHeight      = cfg_.height;
    init.frameRateNum      = cfg_.fps;
    init.frameRateDen      = 1;
    init.enableEncodeAsync = 1; // ← async: no bloquea el hilo de captura
    init.enablePTD         = 1;
    init.encodeConfig      = &enc_cfg;

    st = fn_.nvEncInitializeEncoder(nvenc_session_, &init);
    if (st != NV_ENC_SUCCESS) {
        std::fprintf(stderr, "[NVENC] InitializeEncoder falló: %d\n", st);
        return false;
    }

    // ── Buffer de salida del bitstream ───────────────────────────
    NV_ENC_CREATE_BITSTREAM_BUFFER bs{};
    bs.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    fn_.nvEncCreateBitstreamBuffer(nvenc_session_, &bs);
    bitstream_ = bs.bitstreamBuffer;

    ready_ = true;
    std::printf("[NVENC] Listo — %ux%u @ %u fps, %u Mbps, %s\n",
                cfg_.width, cfg_.height, cfg_.fps,
                cfg_.bitrate / 1'000'000,
                cfg_.use_hevc ? "H.265" : "H.264");
    return true;
}

bool NVENCEncoder::EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                                const PacketCallback& cb) {
    if (!ready_) return false;

    // ► Registrar la textura D3D11 en NVENC sin copiarla (zero-copy)
    NV_ENC_REGISTER_RESOURCE reg{};
    reg.version              = NV_ENC_REGISTER_RESOURCE_VER;
    reg.resourceType         = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    reg.resourceToRegister   = tex;
    reg.width                = cfg_.width;
    reg.height               = cfg_.height;
    reg.bufferFormat         = NV_ENC_BUFFER_FORMAT_ARGB;
    fn_.nvEncRegisterResource(nvenc_session_, &reg);

    NV_ENC_MAP_INPUT_RESOURCE map{};
    map.version            = NV_ENC_MAP_INPUT_RESOURCE_VER;
    map.registeredResource = reg.registeredResource;
    fn_.nvEncMapInputResource(nvenc_session_, &map);

    // ► Codificar: la GPU lee directo la textura DXGI
    NV_ENC_PIC_PARAMS pic{};
    pic.version         = NV_ENC_PIC_PARAMS_VER;
    pic.inputBuffer     = map.mappedResource;
    pic.outputBitstream = bitstream_;
    pic.inputWidth      = cfg_.width;
    pic.inputHeight     = cfg_.height;
    pic.inputPitch      = cfg_.width * 4;
    pic.bufferFmt       = NV_ENC_BUFFER_FORMAT_ARGB;
    pic.pictureType     = NV_ENC_PIC_TYPE_AUTOSELECT;
    pic.inputTimeStamp  = pts_us;
    fn_.nvEncEncodePicture(nvenc_session_, &pic);

    // ► Leer NAL units y entregar al callback
    NV_ENC_LOCK_BITSTREAM lock{};
    lock.version         = NV_ENC_LOCK_BITSTREAM_VER;
    lock.outputBitstream = bitstream_;
    fn_.nvEncLockBitstream(nvenc_session_, &lock);

    cb(static_cast<const uint8_t*>(lock.bitstreamBufferPtr),
       lock.bitstreamSizeInBytes, pts_us);

    fn_.nvEncUnlockBitstream(nvenc_session_, bitstream_);
    fn_.nvEncUnmapInputResource(nvenc_session_, map.mappedResource);
    fn_.nvEncUnregisterResource(nvenc_session_, reg.registeredResource);
    return true;
}

void NVENCEncoder::Shutdown() {
    if (!nvenc_session_) return;
    if (bitstream_) {
        fn_.nvEncDestroyBitstreamBuffer(nvenc_session_, bitstream_);
        bitstream_ = nullptr;
    }
    fn_.nvEncDestroyEncoder(nvenc_session_);
    nvenc_session_ = nullptr;
    ready_ = false;
}
