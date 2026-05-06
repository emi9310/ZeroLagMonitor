#include "QSVEncoder.hpp"
#include <cstdio>
#include <cstring>

bool QSVEncoder::Init(ID3D11Device* device, const EncoderConfig& cfg) {
    device_ = device;
    cfg_    = cfg;

    if (!InitSession())    return false;
    if (!ConfigureEncoder()) return false;

    encoder_ = new MFXVideoENCODE(session_);

    // ── Consultar parámetros reales que el HW soporta ───────────
    mfxVideoParam corrected = video_params_;
    mfxStatus sts = encoder_->Query(&video_params_, &corrected);
    if (sts < MFX_ERR_NONE) {
        std::fprintf(stderr, "[QSV] Parámetros no soportados: %d\n", sts);
        return false;
    }
    video_params_ = corrected;

    sts = encoder_->Init(&video_params_);
    if (sts < MFX_ERR_NONE) {
        std::fprintf(stderr, "[QSV] encoder->Init falló: %d\n", sts);
        return false;
    }

    // ── Buffer de salida bitstream ───────────────────────────────
    mfxVideoParam actual{};
    encoder_->GetVideoParam(&actual);
    bitstream_buf_.resize(actual.mfx.BufferSizeInKB * 1024 * 2);

    ready_ = true;
    std::printf("[QSV] Listo — %ux%u @ %u fps, %u Mbps, %s\n",
                cfg_.width, cfg_.height, cfg_.fps,
                cfg_.bitrate / 1'000'000,
                cfg_.use_hevc ? "H.265" : "H.264");
    return true;
}

bool QSVEncoder::InitSession() {
    mfxIMPL impl   = MFX_IMPL_HARDWARE | MFX_IMPL_VIA_D3D11;
    mfxVersion ver = {{ 0, 1 }};

    mfxStatus sts = session_.Init(impl, &ver);
    if (sts != MFX_ERR_NONE) {
        std::fprintf(stderr, "[QSV] MFXVideoSession::Init falló: %d\n", sts);
        return false;
    }

    // Vincular el ID3D11Device existente (mismo que DXGI capture)
    mfxHDL hdl = reinterpret_cast<mfxHDL>(device_);
    sts = session_.SetHandle(MFX_HANDLE_D3D11_DEVICE, hdl);
    if (sts != MFX_ERR_NONE) {
        std::fprintf(stderr, "[QSV] SetHandle D3D11 falló: %d\n", sts);
        return false;
    }
    return true;
}

bool QSVEncoder::ConfigureEncoder() {
    std::memset(&video_params_, 0, sizeof(video_params_));

    video_params_.mfx.CodecId = cfg_.use_hevc ? MFX_CODEC_HEVC : MFX_CODEC_AVC;

    // Preset de baja latencia
    video_params_.mfx.TargetUsage       = MFX_TARGETUSAGE_BEST_SPEED;
    video_params_.mfx.TargetKbps        = static_cast<mfxU16>(cfg_.bitrate / 1000);
    video_params_.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    video_params_.mfx.GopRefDist        = 1;  // sin B-frames
    video_params_.mfx.GopPicSize        = static_cast<mfxU16>(cfg_.fps * 2); // I cada 2s
    video_params_.mfx.NumRefFrame       = 1;

    video_params_.mfx.FrameInfo.FourCC        = MFX_FOURCC_NV12;
    video_params_.mfx.FrameInfo.ChromaFormat  = MFX_CHROMAFORMAT_YUV420;
    video_params_.mfx.FrameInfo.Width         = (cfg_.width  + 15) & ~15; // alineado a 16
    video_params_.mfx.FrameInfo.Height        = (cfg_.height + 15) & ~15;
    video_params_.mfx.FrameInfo.CropW         = cfg_.width;
    video_params_.mfx.FrameInfo.CropH         = cfg_.height;
    video_params_.mfx.FrameInfo.FrameRateExtN = cfg_.fps;
    video_params_.mfx.FrameInfo.FrameRateExtD = 1;
    video_params_.mfx.FrameInfo.PicStruct     = MFX_PICSTRUCT_PROGRESSIVE;

    // Usar D3D11 memory (zero-copy con textura DXGI)
    video_params_.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

    // Extensiones: baja latencia explícita
    static mfxExtCodingOption  opt{};
    static mfxExtCodingOption2 opt2{};
    opt.Header.BufferId  = MFX_EXTBUFF_CODING_OPTION;
    opt.Header.BufferSz  = sizeof(opt);
    opt.NalHrdConformance = MFX_CODINGOPTION_OFF;
    opt.VuiVclHrdParameters = MFX_CODINGOPTION_OFF;

    opt2.Header.BufferId   = MFX_EXTBUFF_CODING_OPTION2;
    opt2.Header.BufferSz   = sizeof(opt2);
    opt2.MaxSliceSize      = 0;
    opt2.RepeatPPS         = MFX_CODINGOPTION_ON;

    static mfxExtBuffer* ext_bufs[] = {
        reinterpret_cast<mfxExtBuffer*>(&opt),
        reinterpret_cast<mfxExtBuffer*>(&opt2)
    };
    video_params_.ExtParam    = ext_bufs;
    video_params_.NumExtParam = 2;

    return true;
}

bool QSVEncoder::EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                              const PacketCallback& cb) {
    if (!ready_) return false;

    // ► Importar la textura D3D11 como superficie MFX — zero-copy
    mfxFrameSurface1 surface{};
    surface.Info          = video_params_.mfx.FrameInfo;
    surface.Data.MemId    = reinterpret_cast<mfxMemId>(tex); // D3D11 texture handle
    surface.Data.TimeStamp = pts_us;

    mfxSyncPoint sync{};
    mfxBitstream bs{};
    bs.Data       = bitstream_buf_.data();
    bs.MaxLength  = static_cast<mfxU32>(bitstream_buf_.size());

    mfxStatus sts = encoder_->EncodeFrameAsync(nullptr, &surface, &bs, &sync);
    if (sts == MFX_WRN_DEVICE_BUSY) return false; // reintentar en el sig frame
    if (sts < MFX_ERR_NONE)         return false;

    // ► Esperar a que el HW termine (timeout 60ms)
    sts = session_.SyncOperation(sync, 60);
    if (sts != MFX_ERR_NONE) return false;

    cb(bs.Data + bs.DataOffset, bs.DataLength, pts_us);
    return true;
}

void QSVEncoder::Shutdown() {
    if (encoder_) {
        delete encoder_;
        encoder_ = nullptr;
    }
    session_.Close();
    ready_ = false;
}
