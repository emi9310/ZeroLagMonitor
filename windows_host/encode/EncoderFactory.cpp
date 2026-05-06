#include "EncoderFactory.hpp"
#include "NVENCEncoder.hpp"
#include "AMFEncoder.hpp"
#include "QSVEncoder.hpp"
#include "MFEncoder.hpp"
#include "../gpu/GPUDetector.hpp"

#include <cstdio>
#include <string>
#include <array>

// ── Helpers internos ─────────────────────────────────────────────

static const char* EncoderTypeName(EncoderType t) {
    switch (t) {
        case EncoderType::NVENC: return "NVENC (NVIDIA)";
        case EncoderType::AMF:   return "AMF   (AMD)";
        case EncoderType::QSV:   return "Quick Sync (Intel)";
        case EncoderType::MF:    return "MF (Software — universal)";
        default:                 return "Auto";
    }
}

// ── Implementación pública ───────────────────────────────────────

std::unique_ptr<IEncoder> EncoderFactory::Create(EncoderType type,
                                                  ID3D11Device* device,
                                                  const EncoderConfig& cfg) {
    if (type != EncoderType::Auto) {
        return TryCreate(type, device, cfg);
    }

    // Auto: prueba en orden de calidad/latencia; MF es el fallback universal
    constexpr std::array<EncoderType, 4> priority = {
        EncoderType::NVENC,
        EncoderType::AMF,
        EncoderType::QSV,
        EncoderType::MF,
    };

    for (auto t : priority) {
        auto enc = TryCreate(t, device, cfg);
        if (enc) {
            std::printf("[EncoderFactory] Auto-seleccionado: %s\n",
                        EncoderTypeName(t));
            return enc;
        }
        std::printf("[EncoderFactory] %s no disponible, probando siguiente...\n",
                    EncoderTypeName(t));
    }

    std::fprintf(stderr,
        "[EncoderFactory] ERROR: ningún encoder de hardware disponible.\n"
        "  Verifica que los drivers están actualizados.\n");
    return nullptr;
}

std::unique_ptr<IEncoder> EncoderFactory::CreateInteractive(ID3D11Device* device,
                                                             const EncoderConfig& cfg) {
    // ── Detectar y mostrar GPUs ──────────────────────────────────
    GPUDetector detector;
    if (!detector.Detect() || detector.GetAll().empty()) {
        std::fprintf(stderr,
            "[EncoderFactory] No se encontraron GPUs de hardware.\n");
        return nullptr;
    }

    detector.PrintAll();

    // ── Mostrar menú de encoders disponibles ────────────────────
    std::printf("Elige el encoder:\n");
    std::printf("  [0] Auto (recomendado)\n");
    std::printf("  [1] NVENC   — NVIDIA\n");
    std::printf("  [2] AMF     — AMD\n");
    std::printf("  [3] Quick Sync — Intel\n");
    std::printf("Opción [0-3]: ");

    int choice = 0;
    if (std::scanf("%d", &choice) != 1) choice = 0;

    EncoderType selected;
    switch (choice) {
        case 1:  selected = EncoderType::NVENC; break;
        case 2:  selected = EncoderType::AMF;   break;
        case 3:  selected = EncoderType::QSV;   break;
        default: selected = EncoderType::Auto;  break;
    }

    std::printf("\n>>> Usando: %s\n\n", EncoderTypeName(selected));
    return Create(selected, device, cfg);
}

// ── TryCreate ────────────────────────────────────────────────────

std::unique_ptr<IEncoder> EncoderFactory::TryCreate(EncoderType type,
                                                     ID3D11Device* device,
                                                     const EncoderConfig& cfg) {
    std::unique_ptr<IEncoder> enc;

    switch (type) {
        case EncoderType::NVENC: enc = std::make_unique<NVENCEncoder>(); break;
        case EncoderType::AMF:   enc = std::make_unique<AMFEncoder>();   break;
        case EncoderType::QSV:   enc = std::make_unique<QSVEncoder>();   break;
        case EncoderType::MF:    enc = std::make_unique<MFEncoder>();    break;
        default: return nullptr;
    }

    if (!enc->Init(device, cfg)) {
        return nullptr; // Init loguea el motivo del fallo
    }
    return enc;
}
