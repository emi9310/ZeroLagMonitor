#include "../capture/DXGICapture.hpp"
#include "../encode/EncoderFactory.hpp"
#include "../network/AsyncSender.hpp"
#include "../ui/WebUI.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <windows.h>

// ── Estado global compartido entre WebUI y el loop de captura ───
static std::atomic<bool>     g_running{false};
static std::atomic<uint64_t> g_frames{0};
static std::mutex             g_stream_mutex;
static std::atomic<bool>     g_tablet_ok{false};  // tablet detectada vía ADB

// ── ADB forward automático ───────────────────────────────────────

// Devuelve la carpeta donde está MyrrorHost.exe
static std::string GetExeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* sep = strrchr(buf, '\\');
    if (sep) *(sep + 1) = '\0';
    return buf;
}

// ── Virtual Display Driver ───────────────────────────────────────

// Modifica <count>N</count> en el XML de configuración del VDD
static void SetVddCount(int count) {
    const char* xml_path = "C:\\VirtualDisplayDriver\\vdd_settings.xml";
    std::ifstream in(xml_path);
    if (!in) {
        std::printf("[VDD] No se encontró vdd_settings.xml — saltando\n");
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    in.close();

    auto pos = content.find("<count>");
    auto end = content.find("</count>", pos);
    if (pos != std::string::npos && end != std::string::npos) {
        content.replace(pos + 7, end - pos - 7, std::to_string(count));
        std::ofstream out(xml_path);
        if (out) out << content;
    }
}

static std::string g_localappdata;  // cache de %LOCALAPPDATA%

static std::string LocalAppData() {
    if (!g_localappdata.empty()) return g_localappdata;
    char buf[MAX_PATH] = {};
    DWORD len = ExpandEnvironmentStringsA("%LOCALAPPDATA%", buf, MAX_PATH);
    g_localappdata = (len > 0) ? buf : "";
    return g_localappdata;
}

static std::string VddWingetBase() {
    return LocalAppData() +
        "\\Microsoft\\WinGet\\Packages\\"
        "VirtualDrivers.Virtual-Display-Driver_Microsoft.Winget.Source_8wekyb3d8bbwe\\";
}

// Busca devcon.exe en varias ubicaciones conocidas
static std::string FindDevcon() {
    std::string exe_dir = GetExeDir();
    const std::string candidates[] = {
        exe_dir + "vdd\\devcon.exe",
        exe_dir + "devcon.exe",
        VddWingetBase() + "Dependencies\\devcon.exe",
    };
    for (const auto& p : candidates)
        if (!p.empty() && GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;
    return {};
}

// Busca MttVDD.inf en varias ubicaciones conocidas
static std::string FindVddInf() {
    std::string exe_dir = GetExeDir();
    const std::string candidates[] = {
        exe_dir + "vdd\\MttVDD.inf",
        VddWingetBase() + "SignedDrivers\\x86\\VDD\\MttVDD.inf",
    };
    for (const auto& p : candidates)
        if (!p.empty() && GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;
    return {};
}

// Corre devcon.exe con los argumentos dados (sin ventana, espera hasta 10 s)
static bool RunDevcon(const std::string& args) {
    std::string exe = FindDevcon();
    if (exe.empty()) {
        std::printf("[VDD] devcon.exe no encontrado\n");
        return false;
    }
    std::string cmd = "\"" + exe + "\" " + args;

    STARTUPINFOA si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

static void CreateVirtualDisplay() {
    std::string inf = FindVddInf();
    if (inf.empty()) {
        std::printf("[VDD] MttVDD.inf no encontrado — saltando\n");
        return;
    }
    std::printf("[VDD] Creando monitor virtual...\n");
    SetVddCount(1);
    // Eliminar instancias previas para evitar duplicados, luego instalar una
    RunDevcon("remove Root\\MttVDD");
    Sleep(300);
    if (RunDevcon("install \"" + inf + "\" Root\\MttVDD"))
        std::printf("[VDD] Monitor virtual creado.\n");
    else
        std::printf("[VDD] No se pudo crear el monitor virtual.\n");
}

static void RemoveVirtualDisplay() {
    std::printf("[VDD] Eliminando monitor virtual...\n");
    SetVddCount(0);
    RunDevcon("remove Root\\MttVDD");
    std::printf("[VDD] Monitor virtual eliminado.\n");
}

// ── ADB forward automático ───────────────────────────────────────

// Intenta correr: <adb_path> forward tcp:<port> tcp:<port>
// Devuelve true si el proceso termina con exit code 0
static bool TryAdb(const std::string& adb_path, uint16_t port) {
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd),
                  "\"%s\" forward tcp:%u tcp:%u",
                  adb_path.c_str(), port, port);

    STARTUPINFOA si{};
    si.cb        = sizeof(si);
    si.dwFlags   = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// Busca adb.exe y ejecuta el forward. Devuelve true si lo logra.
static bool RunAdbForward(uint16_t port) {
    std::string exe_dir = GetExeDir();

    // Candidatos en orden: junto al .exe, platform-tools del usuario, rutas comunes
    const std::string candidates[] = {
        exe_dir + "adb\\adb.exe",                    // bundleado junto al .exe
        exe_dir + "adb.exe",
        "D:\\Archivos de Programas\\AndroidSDK\\platform-tools\\adb.exe",  // este PC
        "D:\\platform-tools\\adb.exe",
        "C:\\platform-tools\\adb.exe",
        "C:\\Android\\platform-tools\\adb.exe",
        "C:\\Users\\Emiliano Benito\\AppData\\Local\\Android\\Sdk\\platform-tools\\adb.exe",
        "C:\\Users\\Public\\Android\\platform-tools\\adb.exe",
        "adb.exe",   // en PATH
    };

    for (const auto& path : candidates) {
        if (TryAdb(path, port)) return true;
    }
    return false;
}

// Hilo que intenta el forward cada 5 s hasta que la tablet se conecta
static std::thread g_adb_thread;
static std::atomic<bool> g_adb_loop{false};

static void AdbWatchdog(uint16_t port) {
    while (g_adb_loop) {
        bool ok = RunAdbForward(port);
        g_tablet_ok = ok;
        // Si ya está OK, verificar cada 10 s; si no, reintentar cada 5 s
        std::this_thread::sleep_for(
            std::chrono::seconds(ok ? 10 : 5));
    }
}

// Objetos de pipeline — se crean/destruyen en cada Start/Stop
static DXGICapture*          g_capture  = nullptr;
static std::unique_ptr<IEncoder>  g_encoder;
static AsyncSender*          g_sender   = nullptr;
static std::thread           g_capture_thread;

static void OnSignal(int) { g_running = false; }

// ── Hilo de captura + encode + send ─────────────────────────────
static void CaptureLoop(EncoderConfig enc_cfg) {
    while (g_running) {
        bool got = g_capture->AcquireFrame(
            [&](ID3D11Texture2D* tex, uint64_t ts) {
                g_encoder->EncodeFrame(tex, ts,
                    [&](const uint8_t* nal, size_t sz, uint64_t pts) {
                        g_sender->Enqueue(nal, sz, pts);
                    });
                ++g_frames;
            });
        if (!got) YieldProcessor();
    }
}

// ── Callback: WebUI solicita iniciar el stream ───────────────────
static bool OnStart(const StreamConfig& cfg) {
    std::lock_guard<std::mutex> lock(g_stream_mutex);
    if (g_running) return true; // ya corriendo

    // 1. Captura DXGI
    DXGICapture::Config cap_cfg{
        .adapter_index = cfg.adapter_index,
        .output_index  = cfg.monitor_index,
    };
    g_capture = new DXGICapture(cap_cfg);
    if (!g_capture->Init()) {
        delete g_capture; g_capture = nullptr;
        return false;
    }

    // 2. Encoder según elección del usuario
    EncoderConfig enc_cfg{
        .width    = cfg.width,
        .height   = cfg.height,
        .fps      = cfg.fps,
        .bitrate  = cfg.bitrate_mbps * 1'000'000u,
        .use_hevc = cfg.use_hevc,
    };
    g_encoder = EncoderFactory::Create(cfg.encoder_type,
                                       g_capture->GetDevice(), enc_cfg);
    if (!g_encoder) {
        delete g_capture; g_capture = nullptr;
        return false;
    }

    // 3. Sender UDP
    AsyncSender::Config net_cfg{
        .target_ip   = cfg.target_ip.c_str(),
        .target_port = cfg.target_port,
    };
    g_sender = new AsyncSender(net_cfg);
    if (!g_sender->Init()) {
        g_encoder->Shutdown();
        g_encoder.reset();
        delete g_capture; g_capture = nullptr;
        delete g_sender;  g_sender  = nullptr;
        return false;
    }

    // 4. Arrancar hilo de captura
    g_frames  = 0;
    g_running = true;
    g_capture_thread = std::thread(CaptureLoop, enc_cfg);

    std::printf("[Pipeline] Stream iniciado — %s  %ux%u  %ufps  %uMbps  %s  → %s:%u\n",
        g_encoder->GetName(),
        cfg.width, cfg.height, cfg.fps, cfg.bitrate_mbps,
        cfg.use_hevc ? "H.265" : "H.264",
        cfg.target_ip.c_str(), cfg.target_port);
    return true;
}

// ── Callback: WebUI solicita detener el stream ───────────────────
static void OnStop() {
    std::lock_guard<std::mutex> lock(g_stream_mutex);
    if (!g_running) return;

    g_running = false;
    if (g_capture_thread.joinable()) g_capture_thread.join();

    g_sender->Stop();
    g_encoder->Shutdown();
    g_encoder.reset();
    delete g_sender;  g_sender  = nullptr;
    delete g_capture; g_capture = nullptr;

    std::printf("[Pipeline] Stream detenido. Frames totales: %llu\n",
                static_cast<unsigned long long>(g_frames.load()));
}

// ── Callback: WebUI consulta estado ─────────────────────────────
static StreamStatus OnStatus() {
    StreamStatus st;
    st.running      = g_running.load();
    st.frames_sent  = g_frames.load();
    st.encoder_name = g_encoder ? g_encoder->GetName() : "—";
    st.tablet_ok    = g_tablet_ok.load();
    return st;
}

// ────────────────────────────────────────────────────────────────

int main() {
    std::signal(SIGINT, OnSignal);

    // Registrar limpieza para que corra siempre al salir (std::exit, SIGINT, etc.)
    std::atexit(RemoveVirtualDisplay);

    // Crear monitor virtual al arrancar
    CreateVirtualDisplay();

    // Arrancar el watchdog ADB antes de todo
    g_adb_loop = true;
    g_adb_thread = std::thread(AdbWatchdog, 9000);

    WebUI ui(8080);
    ui.SetStartCallback (OnStart);
    ui.SetStopCallback  (OnStop);
    ui.SetStatusCallback(OnStatus);

    // Arranca el servidor HTTP y abre el browser automáticamente
    if (!ui.Start()) {
        std::fprintf(stderr, "No se pudo iniciar la WebUI.\n");
        return 1;
    }

    std::printf("[Myrror] Panel abierto en http://localhost:8080\n");
    std::printf("[Myrror] Ctrl+C para salir.\n");

    // El hilo principal solo espera la señal de salida.
    // Todo lo demás lo maneja la WebUI y los callbacks.
    while (g_running || !g_running) {  // loop hasta SIGINT
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!g_running && g_capture_thread.joinable()) break;
    }

    OnStop();
    ui.Stop();
    g_adb_loop = false;
    if (g_adb_thread.joinable()) g_adb_thread.join();
    return 0;
}
