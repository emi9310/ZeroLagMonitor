#include "GPUDetector.hpp"
#include <dxgi.h>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")

bool GPUDetector::Detect() {
    gpus_.clear();

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        // Ignorar el adaptador software "Microsoft Basic Render Driver"
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter.Reset();
            continue;
        }

        GPUInfo info;
        info.description       = desc.Description;
        info.adapter_index     = i;
        info.dedicated_vram_mb = desc.DedicatedVideoMemory / (1024 * 1024);
        info.vendor            = VendorFromId(desc.VendorId);
        info.preferred_encoder = EncoderFromVendor(info.vendor);

        // Ignorar duplicados — misma descripción y misma VRAM (adaptadores espejo de DXGI)
        bool duplicate = false;
        for (const auto& existing : gpus_) {
            if (existing.description       == info.description &&
                existing.dedicated_vram_mb == info.dedicated_vram_mb) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) gpus_.push_back(std::move(info));
        adapter.Reset();
    }

    // Ordenar: discreta primero (más VRAM = más dedicada)
    std::sort(gpus_.begin(), gpus_.end(), [](const GPUInfo& a, const GPUInfo& b) {
        return a.dedicated_vram_mb > b.dedicated_vram_mb;
    });

    return !gpus_.empty();
}

const GPUInfo* GPUDetector::GetPreferred() const {
    if (gpus_.empty()) return nullptr;
    // La lista ya está ordenada por VRAM desc; la primera es la más potente
    return &gpus_.front();
}

const GPUInfo* GPUDetector::GetByIndex(UINT idx) const {
    for (const auto& g : gpus_) {
        if (g.adapter_index == idx) return &g;
    }
    return nullptr;
}

std::string GPUDetector::GetDisplayString(const GPUInfo& info) const {
    const char* enc_name = "?";
    switch (info.preferred_encoder) {
        case EncoderType::NVENC: enc_name = "NVENC";      break;
        case EncoderType::AMF:   enc_name = "AMF";        break;
        case EncoderType::QSV:   enc_name = "Quick Sync"; break;
        default:                 enc_name = "Software";   break;
    }

    // Convertir wstring a string (ASCII-safe para nombres de GPU)
    std::string desc(info.description.begin(), info.description.end());

    char buf[256];
    std::snprintf(buf, sizeof(buf), "[%u] %s  (%s)  |  %zu MB VRAM",
                  info.adapter_index, desc.c_str(),
                  enc_name, info.dedicated_vram_mb);
    return buf;
}

void GPUDetector::PrintAll() const {
    std::printf("\n=== GPUs detectadas ===\n");
    for (const auto& g : gpus_) {
        std::printf("  %s\n", GetDisplayString(g).c_str());
    }
    std::printf("=======================\n\n");
}

// ── Helpers privados ─────────────────────────────────────────────

GPUVendor GPUDetector::VendorFromId(UINT vendor_id) {
    switch (vendor_id) {
        case 0x10DE: return GPUVendor::NVIDIA;
        case 0x1002: return GPUVendor::AMD;
        case 0x8086: return GPUVendor::Intel;
        default:     return GPUVendor::Unknown;
    }
}

EncoderType GPUDetector::EncoderFromVendor(GPUVendor vendor) {
    switch (vendor) {
        case GPUVendor::NVIDIA: return EncoderType::NVENC;
        case GPUVendor::AMD:    return EncoderType::AMF;
        case GPUVendor::Intel:  return EncoderType::QSV;
        default:                return EncoderType::Auto;
    }
}
