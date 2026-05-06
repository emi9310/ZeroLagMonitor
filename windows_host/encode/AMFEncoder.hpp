#pragma once
#include "IEncoder.hpp"
#include <d3d11.h>
#include <wrl/client.h>

// AMD Advanced Media Framework (AMF) SDK
// Headers en third_party/AMF/ — runtime: amfrt64.dll (viene con drivers AMD)
#include "core/Factory.h"
#include "core/Context.h"
#include "components/VideoEncoderHEVC.h"
#include "components/VideoEncoderVCE.h"

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────
// Encoder AMD AMF  — implementa IEncoder
// Requisito: GPU AMD GCN 2ª gen+ (RX 400+, Vega, RDNA)
// Zero-copy: la textura D3D11 de DXGI se pasa como AMFSurface
//            sin copiar a CPU ni a buffer intermedio.
// ─────────────────────────────────────────────────────────────────
class AMFEncoder final : public IEncoder {
public:
    AMFEncoder() = default;
    ~AMFEncoder() override { Shutdown(); }

    bool        Init(ID3D11Device* device, const EncoderConfig& cfg) override;
    bool        EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                            const PacketCallback& cb) override;
    const char* GetName()  const override { return "AMF"; }
    bool        IsReady()  const override { return ready_; }
    void        Shutdown()       override;

private:
    bool ConfigureHEVC();
    bool ConfigureH264();

    ID3D11Device*               device_    = nullptr;
    EncoderConfig               cfg_{};

    amf::AMFFactory*            factory_   = nullptr; // cargado via LoadLibrary
    amf::AMFContextPtr          context_;
    amf::AMFComponentPtr        encoder_;
    HMODULE                     amf_dll_   = nullptr;

    bool                        ready_     = false;
};
