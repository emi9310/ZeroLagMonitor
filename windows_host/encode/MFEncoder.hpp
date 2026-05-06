#pragma once
#include "IEncoder.hpp"
#include <d3d11.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────
// MFEncoder — Windows Media Foundation H.264/H.265
//
// Funciona en CUALQUIER PC con Windows 8+, sin drivers adicionales.
// Intel/AMD/NVIDIA: usa aceleración por hardware si está disponible.
// Sin GPU: usa software encoding (mayor CPU pero funcional).
// Usa la GPU D3D11 del sistema solo para la copia inicial.
// ─────────────────────────────────────────────────────────────────
class MFEncoder final : public IEncoder {
public:
    MFEncoder() = default;
    ~MFEncoder() override { Shutdown(); }

    bool        Init(ID3D11Device* device, const EncoderConfig& cfg) override;
    bool        EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                            const PacketCallback& cb) override;
    const char* GetName()  const override { return "MF (Software)"; }
    bool        IsReady()  const override { return ready_; }
    void        Shutdown()       override;

private:
    bool SetupTransform();

    ComPtr<IMFTransform>        transform_;
    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<ID3D11Texture2D>     staging_;

    EncoderConfig        cfg_{};
    bool                 ready_      = false;
    bool                 mf_started_ = false;
    LONGLONG             frame_idx_  = 0;
    std::vector<uint8_t> nv12_buf_;
};
