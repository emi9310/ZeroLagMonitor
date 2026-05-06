#pragma once
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

// Fabricante de la GPU detectado via DXGI VendorId
enum class GPUVendor {
    Unknown = 0,
    NVIDIA  = 0x10DE,
    AMD     = 0x1002,
    Intel   = 0x8086,
};

// Encoder preferido para cada GPU
enum class EncoderType {
    Auto,   // Detectar automáticamente según el hardware disponible
    NVENC,  // NVIDIA (Maxwell gen2+)
    AMF,    // AMD  (GCN 2+)
    QSV,    // Intel Quick Sync (Skylake+)
    MF,     // Windows Media Foundation — funciona en cualquier PC
};

struct GPUInfo {
    std::wstring description;   // "NVIDIA GeForce RTX 4080"
    GPUVendor    vendor;
    EncoderType  preferred_encoder;
    UINT         adapter_index;
    size_t       dedicated_vram_mb;
};

// ─────────────────────────────────────────────────────────────────
// Enumera todos los adaptadores DXGI del sistema y extrae:
//   - Fabricante (NVIDIA / AMD / Intel)
//   - Encoder recomendado para baja latencia
// ─────────────────────────────────────────────────────────────────
class GPUDetector {
public:
    // Escanea todos los adaptadores DXGI disponibles.
    // Llama a este método antes de usar cualquier otra función.
    bool Detect();

    // Lista completa de GPUs encontradas (incluye integradas)
    const std::vector<GPUInfo>& GetAll() const { return gpus_; }

    // Primera GPU discreta encontrada (prioridad: NVIDIA > AMD > Intel)
    // Devuelve nullptr si solo hay GPU integrada
    const GPUInfo* GetPreferred() const;

    // Busca la GPU en el índice `adapter_index`
    const GPUInfo* GetByIndex(UINT idx) const;

    // String legible para mostrar al usuario, ej:
    // "[0] NVIDIA GeForce RTX 4080 (NVENC) | 8192 MB"
    std::string GetDisplayString(const GPUInfo& info) const;

    // Imprime en stdout todas las GPUs detectadas (útil para UI de selección)
    void PrintAll() const;

private:
    static GPUVendor  VendorFromId(UINT vendor_id);
    static EncoderType EncoderFromVendor(GPUVendor vendor);

    std::vector<GPUInfo> gpus_;
};
