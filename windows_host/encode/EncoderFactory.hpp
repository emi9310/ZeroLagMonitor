#pragma once
#include "IEncoder.hpp"
#include "../gpu/GPUDetector.hpp"
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────
// EncoderFactory
//
// Crea el encoder correcto según la GPU que el usuario eligió
// (o auto-detecta la mejor opción disponible).
//
// Uso típico:
//   GPUDetector detector;
//   detector.Detect();
//   detector.PrintAll();             // muestra al usuario las opciones
//
//   // El usuario elige por índice o "auto"
//   auto enc = EncoderFactory::Create(EncoderType::NVENC, device, cfg);
//   if (!enc) { /* fallback o error */ }
// ─────────────────────────────────────────────────────────────────
class EncoderFactory {
public:
    // Crea el encoder del tipo solicitado.
    // EncoderType::Auto → prueba NVENC > AMF > QSV en ese orden.
    // Devuelve nullptr si el hardware no está disponible.
    static std::unique_ptr<IEncoder> Create(EncoderType type,
                                            ID3D11Device* device,
                                            const EncoderConfig& cfg);

    // Conveniencia: detecta GPUs, muestra la lista interactiva en consola
    // y devuelve el encoder que el usuario eligió.
    // Pasa EncoderType::Auto si quieres saltar la pregunta.
    static std::unique_ptr<IEncoder> CreateInteractive(ID3D11Device* device,
                                                       const EncoderConfig& cfg);

private:
    // Intenta crear e inicializar un encoder; devuelve nullptr si falla
    static std::unique_ptr<IEncoder> TryCreate(EncoderType type,
                                               ID3D11Device* device,
                                               const EncoderConfig& cfg);
};
