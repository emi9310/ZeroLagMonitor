#include "MFEncoder.hpp"

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <Mfobjects.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// H.264 Software Encoder MFT — Windows 8+
// {6CA50344-051A-4DED-9779-A43305165E35}
static const GUID CLSID_MF_H264 = {
    0x6CA50344, 0x051A, 0x4DED,
    {0x97, 0x79, 0xA4, 0x33, 0x05, 0x16, 0x5E, 0x35}
};

// H.265/HEVC Software Encoder MFT — Windows 10+
// {f2f84074-8bca-40f0-8ae4-0a5a58f49de3}
static const GUID CLSID_MF_HEVC = {
    0xf2f84074, 0x8bca, 0x40f0,
    {0x8a, 0xe4, 0x0a, 0x5a, 0x58, 0xf4, 0x9d, 0xe3}
};

// ── Conversión BGRA → NV12 (BT.601) ─────────────────────────────
// NV12: plano Y (WxH) + plano UV intercalado (W x H/2)
static void BgraToNv12(const uint8_t* bgra, UINT stride,
                        uint8_t* y_plane, uint8_t* uv_plane,
                        UINT width, UINT height)
{
    // Plano Y (luma)
    for (UINT row = 0; row < height; ++row) {
        const uint8_t* src = bgra + row * stride;
        uint8_t*       dst = y_plane + row * width;
        for (UINT col = 0; col < width; ++col) {
            int b = src[col*4+0], g = src[col*4+1], r = src[col*4+2];
            dst[col] = (uint8_t)std::clamp((66*r + 129*g + 25*b + 128) / 256 + 16, 0, 255);
        }
    }
    // Plano UV (chroma 4:2:0 subsampled 2x2)
    for (UINT row = 0; row < height / 2; ++row) {
        const uint8_t* src = bgra + (row * 2) * stride;
        uint8_t*       dst = uv_plane + row * width;
        for (UINT col = 0; col < width / 2; ++col) {
            int b = src[col*2*4+0], g = src[col*2*4+1], r = src[col*2*4+2];
            dst[col*2+0] = (uint8_t)std::clamp((-38*r - 74*g + 112*b + 128) / 256 + 128, 0, 255); // U
            dst[col*2+1] = (uint8_t)std::clamp((112*r - 94*g -  18*b + 128) / 256 + 128, 0, 255); // V
        }
    }
}

// ────────────────────────────────────────────────────────────────

bool MFEncoder::Init(ID3D11Device* device, const EncoderConfig& cfg) {
    cfg_    = cfg;
    device_ = device;
    device_->GetImmediateContext(&ctx_);

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        std::printf("[MF] MFStartup falló: %08lX\n", hr);
        return false;
    }
    mf_started_ = true;

    // Textura staging para leer de GPU a CPU
    D3D11_TEXTURE2D_DESC td{};
    td.Width              = cfg.width;
    td.Height             = cfg.height;
    td.MipLevels          = 1;
    td.ArraySize          = 1;
    td.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count   = 1;
    td.Usage              = D3D11_USAGE_STAGING;
    td.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

    hr = device_->CreateTexture2D(&td, nullptr, &staging_);
    if (FAILED(hr)) {
        std::printf("[MF] CreateTexture2D staging falló: %08lX\n", hr);
        return false;
    }

    // Buffer NV12: Y + UV
    nv12_buf_.resize(cfg.width * cfg.height * 3 / 2);

    if (!SetupTransform()) return false;

    ready_ = true;
    std::printf("[MF] Encoder listo — %ux%u %s (Software)\n",
                cfg.width, cfg.height, cfg.use_hevc ? "H.265" : "H.264");
    return true;
}

bool MFEncoder::SetupTransform() {
    // Intentar HEVC primero, caer a H.264 si no está disponible
    const GUID* codec = cfg_.use_hevc ? &CLSID_MF_HEVC : &CLSID_MF_H264;

    HRESULT hr = CoCreateInstance(*codec, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IMFTransform,
                                  reinterpret_cast<void**>(transform_.GetAddressOf()));
    if (FAILED(hr) && cfg_.use_hevc) {
        std::printf("[MF] HEVC no disponible, usando H.264\n");
        codec = &CLSID_MF_H264;
        hr = CoCreateInstance(*codec, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IMFTransform,
                              reinterpret_cast<void**>(transform_.GetAddressOf()));
    }
    if (FAILED(hr)) {
        std::printf("[MF] No se pudo crear el encoder MFT: %08lX\n", hr);
        return false;
    }

    // Tipo de salida: H.264 o H.265
    ComPtr<IMFMediaType> out_type;
    MFCreateMediaType(&out_type);
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out_type->SetGUID(MF_MT_SUBTYPE,
                      (codec == &CLSID_MF_HEVC) ? MFVideoFormat_HEVC : MFVideoFormat_H264);
    MFSetAttributeSize(out_type.Get(), MF_MT_FRAME_SIZE, cfg_.width, cfg_.height);
    MFSetAttributeRatio(out_type.Get(), MF_MT_FRAME_RATE, cfg_.fps, 1);
    out_type->SetUINT32(MF_MT_AVG_BITRATE, cfg_.bitrate);
    out_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    hr = transform_->SetOutputType(0, out_type.Get(), 0);
    if (FAILED(hr)) {
        std::printf("[MF] SetOutputType falló: %08lX\n", hr);
        return false;
    }

    // Tipo de entrada: NV12
    ComPtr<IMFMediaType> in_type;
    MFCreateMediaType(&in_type);
    in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(in_type.Get(), MF_MT_FRAME_SIZE, cfg_.width, cfg_.height);
    MFSetAttributeRatio(in_type.Get(), MF_MT_FRAME_RATE, cfg_.fps, 1);
    in_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    hr = transform_->SetInputType(0, in_type.Get(), 0);
    if (FAILED(hr)) {
        std::printf("[MF] SetInputType falló: %08lX\n", hr);
        return false;
    }

    // Configurar bitrate y baja latencia vía ICodecAPI
    ComPtr<ICodecAPI> codec_api;
    if (SUCCEEDED(transform_.As(&codec_api))) {
        VARIANT var{};
        var.vt = VT_UI4; var.ulVal = cfg_.bitrate;
        codec_api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var);

        var.vt = VT_BOOL; var.boolVal = VARIANT_TRUE;
        codec_api->SetValue(&CODECAPI_AVLowLatencyMode, &var);
    }

    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

bool MFEncoder::EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                             const PacketCallback& cb) {
    if (!ready_) return false;

    // 1. GPU → staging (CPU-readable)
    ctx_->CopyResource(staging_.Get(), tex);

    // 2. Mapear staging
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    // 3. Convertir BGRA → NV12
    uint8_t* y_plane  = nv12_buf_.data();
    uint8_t* uv_plane = y_plane + cfg_.width * cfg_.height;
    BgraToNv12(static_cast<const uint8_t*>(mapped.pData),
               mapped.RowPitch, y_plane, uv_plane, cfg_.width, cfg_.height);
    ctx_->Unmap(staging_.Get(), 0);

    // 4. Crear IMFSample con los datos NV12
    ComPtr<IMFSample>      sample;
    ComPtr<IMFMediaBuffer> buffer;
    DWORD nv12_size = static_cast<DWORD>(nv12_buf_.size());

    MFCreateMemoryBuffer(nv12_size, &buffer);
    BYTE* dst = nullptr;
    buffer->Lock(&dst, nullptr, nullptr);
    std::memcpy(dst, nv12_buf_.data(), nv12_size);
    buffer->Unlock();
    buffer->SetCurrentLength(nv12_size);

    MFCreateSample(&sample);
    sample->AddBuffer(buffer.Get());

    LONGLONG hns_time     = static_cast<LONGLONG>(pts_us) * 10; // μs → 100ns
    LONGLONG hns_duration = 10'000'000LL / cfg_.fps;
    sample->SetSampleTime(hns_time);
    sample->SetSampleDuration(hns_duration);

    // 5. Alimentar al encoder
    HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
    if (FAILED(hr)) return false;

    // 6. Recolectar NAL units de salida
    MFT_OUTPUT_STREAM_INFO stream_info{};
    transform_->GetOutputStreamInfo(0, &stream_info);
    bool provides = (stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;

    while (true) {
        MFT_OUTPUT_DATA_BUFFER output{};
        ComPtr<IMFSample>      out_sample;
        ComPtr<IMFMediaBuffer> out_buffer;

        if (!provides) {
            DWORD buf_size = stream_info.cbSize ? stream_info.cbSize : (1024 * 1024);
            MFCreateMemoryBuffer(buf_size, &out_buffer);
            MFCreateSample(&out_sample);
            out_sample->AddBuffer(out_buffer.Get());
            output.pSample = out_sample.Get();
        }

        DWORD status = 0;
        hr = transform_->ProcessOutput(0, 1, &output, &status);
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) break;
        if (FAILED(hr)) break;

        IMFSample* result = provides ? output.pSample : out_sample.Get();
        if (!result) break;

        ComPtr<IMFMediaBuffer> result_buf;
        result->ConvertToContiguousBuffer(&result_buf);
        BYTE*  data     = nullptr;
        DWORD  data_len = 0;
        result_buf->Lock(&data, nullptr, &data_len);
        if (data && data_len > 0) cb(data, data_len, pts_us);
        result_buf->Unlock();

        if (provides && output.pSample)  output.pSample->Release();
        if (output.pEvents)              output.pEvents->Release();
    }

    ++frame_idx_;
    return true;
}

void MFEncoder::Shutdown() {
    if (!ready_ && !mf_started_) return;
    if (transform_) {
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        transform_.Reset();
    }
    staging_.Reset();
    ctx_.Reset();
    device_.Reset();
    if (mf_started_) { MFShutdown(); mf_started_ = false; }
    ready_ = false;
}
