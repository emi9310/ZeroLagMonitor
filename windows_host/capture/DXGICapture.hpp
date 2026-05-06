#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Callback entrega la textura directamente en GPU - NO hay copia a CPU
using FrameCallback = std::function<void(ID3D11Texture2D*, UINT64 /*timestamp_us*/)>;

class DXGICapture {
public:
    struct Config {
        UINT adapter_index = 0;   // GPU a usar
        UINT output_index  = 0;   // Monitor a capturar (0=primario, 1=segundo, ...)
        UINT timeout_ms    = 5;   // Máx espera por frame
    };

    struct MonitorInfo {
        UINT         output_index;    // índice dentro del adaptador
        UINT         adapter_index;   // qué adaptador DXGI lo maneja
        UINT         width, height;
        bool         is_primary;
        std::wstring device_name;
    };

    explicit DXGICapture(Config cfg);
    ~DXGICapture();

    bool Init();
    // Llama callback con la GPU texture compuesta (frame + cursor)
    bool AcquireFrame(const FrameCallback& cb);
    void ReleaseFrame();

    ID3D11Device*        GetDevice()  const { return d3d_device_.Get(); }
    ID3D11DeviceContext* GetContext() const { return d3d_ctx_.Get();    }

    // Enumera los monitores disponibles en el adaptador dado
    static std::vector<MonitorInfo> EnumMonitors(UINT adapter_index = 0);

private:
    // ── Cursor ───────────────────────────────────────────────────
    struct CursorState {
        bool    visible    = false;
        LONG    x = 0, y = 0;          // posición en pantalla
        UINT    width  = 0;
        UINT    height = 0;
        UINT    pitch_bgra = 0;         // bytes por fila en bgra[]
        DXGI_OUTDUPL_POINTER_SHAPE_TYPE type{};
        std::vector<uint8_t> bgra;      // píxeles BGRA del cursor
    };

    bool EnsureCompositingTex(UINT w, UINT h);
    void UpdateCursorShape(const DXGI_OUTDUPL_FRAME_INFO& info);
    void CompositeCursor();

    Config cfg_;
    ComPtr<ID3D11Device>            d3d_device_;
    ComPtr<ID3D11DeviceContext>     d3d_ctx_;
    ComPtr<IDXGIOutputDuplication>  duplication_;

    // Textura de composición: copia del frame con cursor superpuesto
    ComPtr<ID3D11Texture2D>         comp_tex_;
    // Textura staging pequeña para leer/escribir la región del cursor (CPU)
    ComPtr<ID3D11Texture2D>         cursor_stage_;
    UINT                            comp_w_ = 0, comp_h_ = 0;

    CursorState                     cursor_;
};
