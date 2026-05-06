#pragma once
#include "IEncoder.hpp"

// QSV no disponible en GPU AMD — stub que siempre devuelve false
class QSVEncoder final : public IEncoder {
public:
    bool        Init(ID3D11Device*, const EncoderConfig&) override {
        return false; // no hay GPU Intel
    }
    bool        EncodeFrame(ID3D11Texture2D*, uint64_t,
                            const PacketCallback&) override { return false; }
    const char* GetName()  const override { return "Quick Sync"; }
    bool        IsReady()  const override { return false; }
    void        Shutdown()       override {}
};
