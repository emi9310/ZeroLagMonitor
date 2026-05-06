#pragma once
#include "IEncoder.hpp"

// NVENC no disponible en GPU AMD — stub que siempre devuelve false
// EncoderFactory lo instancia pero Init() falla limpiamente
class NVENCEncoder final : public IEncoder {
public:
    bool        Init(ID3D11Device*, const EncoderConfig&) override {
        return false; // no hay GPU NVIDIA
    }
    bool        EncodeFrame(ID3D11Texture2D*, uint64_t,
                            const PacketCallback&) override { return false; }
    const char* GetName()  const override { return "NVENC"; }
    bool        IsReady()  const override { return false; }
    void        Shutdown()       override {}
};
