#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <string>

// Callback entregado por el encoder con cada NAL unit comprimida
using PacketCallback = std::function<void(const uint8_t* data, size_t size, uint64_t pts_us)>;

// Configuración compartida por todos los encoders
struct EncoderConfig {
    uint32_t width    = 1920;
    uint32_t height   = 1080;
    uint32_t fps      = 60;
    uint32_t bitrate  = 20'000'000; // 20 Mbps
    bool     use_hevc = true;       // true=H265, false=H264
};

// ─────────────────────────────────────────────────────────────────
// Interfaz abstracta: todos los encoders de hardware la implementan.
// El pipeline solo conoce IEncoder — no importa si es NVENC, AMF o QSV.
// ─────────────────────────────────────────────────────────────────
class IEncoder {
public:
    virtual ~IEncoder() = default;

    // Inicializa el encoder. Devuelve false si el hardware no está disponible.
    virtual bool Init(ID3D11Device* device, const EncoderConfig& cfg) = 0;

    // Codifica un frame. La textura viene de DXGI y DEBE quedarse en GPU
    // (sin copia a CPU). El callback se invoca con el NAL unit resultante.
    virtual bool EncodeFrame(ID3D11Texture2D* tex, uint64_t pts_us,
                             const PacketCallback& cb) = 0;

    // Nombre legible del encoder para logs y UI ("NVENC", "AMF", "QSV")
    virtual const char* GetName() const = 0;

    // true si el encoder fue inicializado correctamente
    virtual bool IsReady() const = 0;

    // Libera recursos del encoder (también llamado por el destructor)
    virtual void Shutdown() = 0;
};
