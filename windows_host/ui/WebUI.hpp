#pragma once
#include "../encode/EncoderFactory.hpp"
#include "../gpu/GPUDetector.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

// ─────────────────────────────────────────────────────────────────
// Callbacks que el Pipeline registra para que la UI pueda
// arrancar/parar el stream y consultar el estado.
// ─────────────────────────────────────────────────────────────────
struct StreamConfig {
    EncoderType encoder_type = EncoderType::Auto;
    uint32_t    width        = 1920;
    uint32_t    height       = 1080;
    uint32_t    fps          = 60;
    uint32_t    bitrate_mbps = 20;
    bool        use_hevc     = true;
    bool        use_usb      = true;   // true=USB/ADB, false=WiFi/UDP
    std::string target_ip    = "127.0.0.1";
    uint16_t    target_port  = 9000;
    uint32_t    monitor_index  = 0;    // output_index dentro del adaptador
    uint32_t    adapter_index  = 0;    // adaptador DXGI que maneja ese monitor
};

struct StreamStatus {
    bool        running       = false;
    uint64_t    frames_sent   = 0;
    std::string encoder_name  = "—";
    std::string error_msg;
    bool        tablet_ok     = false;  // ADB forward activo
};

using StartCallback  = std::function<bool(const StreamConfig&)>;
using StopCallback   = std::function<void()>;
using StatusCallback = std::function<StreamStatus()>;

// ─────────────────────────────────────────────────────────────────
// WebUI — servidor HTTP local en el puerto 8080.
// Sirve el panel HTML y expone una REST API mínima:
//
//   GET  /              → HTML del panel
//   GET  /api/gpus      → JSON con GPUs detectadas
//   GET  /api/status    → JSON con estado actual del stream
//   POST /api/start     → inicia el stream (body JSON con StreamConfig)
//   POST /api/stop      → detiene el stream
// ─────────────────────────────────────────────────────────────────
class WebUI {
public:
    explicit WebUI(uint16_t port = 8080);
    ~WebUI();

    void SetStartCallback (StartCallback  cb) { on_start_  = std::move(cb); }
    void SetStopCallback  (StopCallback   cb) { on_stop_   = std::move(cb); }
    void SetStatusCallback(StatusCallback cb) { on_status_ = std::move(cb); }

    // Inicia el servidor en un hilo de fondo y abre el browser
    bool Start();
    void Stop();

private:
    void Serve();                   // hilo del servidor HTTP
    void OpenBrowser() const;       // ShellExecute localhost:port

    std::string BuildGPUsJson()      const;
    std::string BuildMonitorsJson()  const;
    std::string BuildStatusJson()    const;
    std::string ParseAndStart(const std::string& body); // devuelve JSON result

    uint16_t      port_;
    std::thread   thread_;
    std::atomic<bool> running_{false};

    StartCallback   on_start_;
    StopCallback    on_stop_;
    StatusCallback  on_status_;

    GPUDetector     detector_;
};
