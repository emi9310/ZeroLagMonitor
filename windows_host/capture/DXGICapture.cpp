#include "DXGICapture.hpp"
#include <algorithm>
#include <cstring>

DXGICapture::DXGICapture(Config cfg) : cfg_(std::move(cfg)) {}
DXGICapture::~DXGICapture() { duplication_.Reset(); }

bool DXGICapture::Init() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Buscar el adaptador DXGI que tiene el monitor pedido
    // (el monitor virtual puede estar en un adaptador distinto al de la GPU física)
    ComPtr<IDXGIFactory1> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    ComPtr<IDXGIAdapter1> target_adapter;
    factory->EnumAdapters1(cfg_.adapter_index, target_adapter.GetAddressOf());

    // Con adaptador específico: tipo UNKNOWN (no HARDWARE)
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(
        target_adapter.Get(),
        target_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
        d3d_device_.GetAddressOf(), &feature_level,
        d3d_ctx_.GetAddressOf()
    );
    if (FAILED(hr)) {
        std::fprintf(stderr, "[DXGI] D3D11CreateDevice falló: 0x%08X\n", hr);
        return false;
    }

    ComPtr<IDXGIOutput>  output;
    ComPtr<IDXGIOutput1> output1;

    if (target_adapter->EnumOutputs(cfg_.output_index, &output) != S_OK) {
        std::fprintf(stderr, "[DXGI] Monitor %u no encontrado en adaptador %u.\n",
                     cfg_.output_index, cfg_.adapter_index);
        return false;
    }
    output->QueryInterface(IID_PPV_ARGS(&output1));

    hr = output1->DuplicateOutput(d3d_device_.Get(), duplication_.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr, "[DXGI] DuplicateOutput falló: 0x%08X\n", hr);
        return false;
    }
    return true;
}

// ── Textura de composición ────────────────────────────────────────

bool DXGICapture::EnsureCompositingTex(UINT w, UINT h) {
    if (comp_tex_ && comp_w_ == w && comp_h_ == h) return true;

    comp_tex_.Reset();
    cursor_stage_.Reset();

    // Textura DEFAULT para componer frame + cursor y pasarla a AMF
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width             = w;
    desc.Height            = h;
    desc.MipLevels         = 1;
    desc.ArraySize         = 1;
    desc.Format            = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count  = 1;
    desc.Usage             = D3D11_USAGE_DEFAULT;
    desc.BindFlags         = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, comp_tex_.GetAddressOf());
    if (FAILED(hr)) return false;

    // Staging pequeño (máx 256×256) para leer/escribir la región del cursor
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width              = 256;
    sd.Height             = 256;
    sd.MipLevels          = 1;
    sd.ArraySize          = 1;
    sd.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count   = 1;
    sd.Usage              = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags     = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

    d3d_device_->CreateTexture2D(&sd, nullptr, cursor_stage_.GetAddressOf());

    comp_w_ = w;
    comp_h_ = h;
    return true;
}

// ── Actualizar forma del cursor ───────────────────────────────────

void DXGICapture::UpdateCursorShape(const DXGI_OUTDUPL_FRAME_INFO& info) {
    if (info.PointerShapeBufferSize == 0) return;

    std::vector<uint8_t> raw(info.PointerShapeBufferSize);
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info{};
    UINT required = 0;

    HRESULT hr = duplication_->GetFramePointerShape(
        info.PointerShapeBufferSize, raw.data(), &required, &shape_info);
    if (FAILED(hr)) return;

    cursor_.type  = static_cast<DXGI_OUTDUPL_POINTER_SHAPE_TYPE>(shape_info.Type);
    cursor_.width = shape_info.Width;

    switch (cursor_.type) {

    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR: {
        cursor_.height     = shape_info.Height;
        cursor_.pitch_bgra = shape_info.Width * 4;
        cursor_.bgra.resize(cursor_.height * cursor_.pitch_bgra);

        for (UINT row = 0; row < cursor_.height; ++row) {
            memcpy(cursor_.bgra.data() + row * cursor_.pitch_bgra,
                   raw.data()          + row * shape_info.Pitch,
                   cursor_.width * 4);
        }
        break;
    }

    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME: {
        // Mitad superior = AND mask, mitad inferior = XOR mask (1 bpp)
        cursor_.height     = shape_info.Height / 2;
        cursor_.pitch_bgra = shape_info.Width  * 4;
        cursor_.bgra.assign(cursor_.height * cursor_.pitch_bgra, 0);

        for (UINT row = 0; row < cursor_.height; ++row) {
            for (UINT col = 0; col < cursor_.width; ++col) {
                UINT byte_and = row                   * shape_info.Pitch + col / 8;
                UINT byte_xor = (row + cursor_.height)* shape_info.Pitch + col / 8;
                UINT bit      = 7 - (col % 8);
                bool and_bit  = (raw[byte_and] >> bit) & 1;
                bool xor_bit  = (raw[byte_xor] >> bit) & 1;

                uint8_t* px = cursor_.bgra.data() + row * cursor_.pitch_bgra + col * 4;
                if      (!and_bit && !xor_bit) { px[0]=px[1]=px[2]=  0; px[3]=255; } // negro
                else if (!and_bit &&  xor_bit) { px[0]=px[1]=px[2]=255; px[3]=255; } // blanco
                else if ( and_bit && !xor_bit) {                         px[3]=  0; } // transparente
                else                           { px[0]=px[1]=px[2]=128; px[3]=255; } // gris (XOR)
            }
        }
        break;
    }

    case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR: {
        cursor_.height     = shape_info.Height;
        cursor_.pitch_bgra = shape_info.Width * 4;
        cursor_.bgra.resize(cursor_.height * cursor_.pitch_bgra);

        for (UINT row = 0; row < cursor_.height; ++row) {
            const uint8_t* src = raw.data()          + row * shape_info.Pitch;
            uint8_t*       dst = cursor_.bgra.data()  + row * cursor_.pitch_bgra;
            for (UINT col = 0; col < cursor_.width; ++col) {
                dst[col*4+0] = src[col*4+0];
                dst[col*4+1] = src[col*4+1];
                dst[col*4+2] = src[col*4+2];
                dst[col*4+3] = (src[col*4+3] > 0) ? 255 : 0; // tratar como opaco si no transparente
            }
        }
        break;
    }

    default: break;
    }
}

// ── Compositar cursor sobre comp_tex_ ────────────────────────────

void DXGICapture::CompositeCursor() {
    if (!cursor_.visible || cursor_.bgra.empty() || !comp_tex_ || !cursor_stage_) return;

    LONG cx = cursor_.x;
    LONG cy = cursor_.y;
    UINT cw = cursor_.width;
    UINT ch = cursor_.height;

    // Descartar cursor completamente fuera de pantalla
    if (cx >= (LONG)comp_w_ || cy >= (LONG)comp_h_) return;
    if (cx + (LONG)cw <= 0  || cy + (LONG)ch <= 0)  return;

    // Calcular offset de origen si el cursor está parcialmente fuera
    UINT src_x = 0, src_y = 0;
    if (cx < 0) { src_x = (UINT)(-cx); cw -= src_x; cx = 0; }
    if (cy < 0) { src_y = (UINT)(-cy); ch -= src_y; cy = 0; }
    if ((UINT)cx + cw > comp_w_) cw = comp_w_ - (UINT)cx;
    if ((UINT)cy + ch > comp_h_) ch = comp_h_ - (UINT)cy;
    if (cw == 0 || ch == 0) return;

    // Limitar al tamaño del staging (256×256)
    cw = std::min(cw, 256u);
    ch = std::min(ch, 256u);

    // 1. Copiar región del frame a staging (GPU → CPU), solo el área del cursor
    D3D11_BOX frame_box{ (UINT)cx, (UINT)cy, 0, (UINT)cx + cw, (UINT)cy + ch, 1 };
    d3d_ctx_->CopySubresourceRegion(
        cursor_stage_.Get(), 0, 0, 0, 0,
        comp_tex_.Get(),     0, &frame_box);

    // 2. Mapear staging y alpha-blend el cursor sobre los píxeles del frame
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(d3d_ctx_->Map(cursor_stage_.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped)))
        return;

    auto* bg      = static_cast<uint8_t*>(mapped.pData);
    UINT  bg_pitch = mapped.RowPitch;

    for (UINT row = 0; row < ch; ++row) {
        const uint8_t* cur = cursor_.bgra.data()
                           + (src_y + row) * cursor_.pitch_bgra
                           + src_x * 4;
        uint8_t* dst = bg + row * bg_pitch;

        for (UINT col = 0; col < cw; ++col) {
            float a = cur[col*4 + 3] / 255.0f;
            if (a < 0.004f) continue;  // píxel completamente transparente
            dst[col*4+0] = (uint8_t)(cur[col*4+0] * a + dst[col*4+0] * (1.0f - a) + 0.5f);
            dst[col*4+1] = (uint8_t)(cur[col*4+1] * a + dst[col*4+1] * (1.0f - a) + 0.5f);
            dst[col*4+2] = (uint8_t)(cur[col*4+2] * a + dst[col*4+2] * (1.0f - a) + 0.5f);
            dst[col*4+3] = 255;
        }
    }

    d3d_ctx_->Unmap(cursor_stage_.Get(), 0);

    // 3. Copiar región compositeada de vuelta al frame (CPU → GPU)
    D3D11_BOX stage_box{ 0, 0, 0, cw, ch, 1 };
    d3d_ctx_->CopySubresourceRegion(
        comp_tex_.Get(),     0, (UINT)cx, (UINT)cy, 0,
        cursor_stage_.Get(), 0, &stage_box);
}

// ── AcquireFrame principal ────────────────────────────────────────

bool DXGICapture::AcquireFrame(const FrameCallback& cb) {
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource>   resource;

    HRESULT hr = duplication_->AcquireNextFrame(
        cfg_.timeout_ms, &info, resource.GetAddressOf());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;  // sin cambios en pantalla
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> tex;
    resource->QueryInterface(IID_PPV_ARGS(&tex));

    // Actualizar posición del cursor
    if (info.PointerPosition.Visible) {
        cursor_.visible = true;
        cursor_.x = info.PointerPosition.Position.x;
        cursor_.y = info.PointerPosition.Position.y;
    } else {
        cursor_.visible = false;
    }

    // Actualizar forma del cursor si cambió
    UpdateCursorShape(info);

    // Timestamp en microsegundos desde QPC
    LARGE_INTEGER qpc; QueryPerformanceCounter(&qpc);
    UINT64 ts = static_cast<UINT64>(qpc.QuadPart);

    // Asegurar textura de composición del tamaño correcto
    D3D11_TEXTURE2D_DESC tex_desc{};
    tex->GetDesc(&tex_desc);
    if (!EnsureCompositingTex(tex_desc.Width, tex_desc.Height)) {
        cb(tex.Get(), ts);  // fallback sin cursor
        duplication_->ReleaseFrame();
        return true;
    }

    // Copiar frame completo a comp_tex_ y superponer cursor
    d3d_ctx_->CopyResource(comp_tex_.Get(), tex.Get());
    CompositeCursor();

    // ► La textura compositeada vive en GPU — cero copias a CPU
    cb(comp_tex_.Get(), ts);

    duplication_->ReleaseFrame();
    return true;
}

// ── Enumeración de monitores ──────────────────────────────────────
// Itera todos los adaptadores para no depender del índice 0

std::vector<DXGICapture::MonitorInfo> DXGICapture::EnumMonitors(UINT /*ignored*/) {
    std::vector<MonitorInfo> monitors;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return monitors;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adap = 0;
         factory->EnumAdapters1(adap, adapter.ReleaseAndGetAddressOf()) == S_OK;
         ++adap)
    {
        DXGI_ADAPTER_DESC1 adesc{};
        adapter->GetDesc1(&adesc);
        // Ignorar adaptadores software (Microsoft Basic Render Driver, etc.)
        if (adesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        ComPtr<IDXGIOutput> output;
        for (UINT i = 0;
             adapter->EnumOutputs(i, output.ReleaseAndGetAddressOf()) == S_OK;
             ++i)
        {
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);

            // Saltar salidas sin superficie de escritorio asignada
            if (desc.DesktopCoordinates.right == desc.DesktopCoordinates.left) continue;

            MonitorInfo mi;
            mi.output_index  = i;
            mi.adapter_index = adap;
            mi.device_name   = desc.DeviceName;
            mi.width         = static_cast<UINT>(desc.DesktopCoordinates.right
                                               - desc.DesktopCoordinates.left);
            mi.height        = static_cast<UINT>(desc.DesktopCoordinates.bottom
                                               - desc.DesktopCoordinates.top);
            mi.is_primary    = (desc.DesktopCoordinates.left == 0 &&
                                desc.DesktopCoordinates.top  == 0);
            monitors.push_back(mi);
        }
    }
    return monitors;
}
