#include "WebUI.hpp"
#include "../capture/DXGICapture.hpp"

// cpp-httplib — HTTP sin SSL (solo localhost, no necesitamos TLS)
// Desactivar OpenSSL antes de incluir para evitar dependencia externa
#undef  CPPHTTPLIB_OPENSSL_SUPPORT
#undef  CPPHTTPLIB_ZLIB_SUPPORT
#undef  CPPHTTPLIB_BROTLI_SUPPORT
#include "../third_party/httplib.h"

#include <shellapi.h>   // ShellExecute
#include <cstdio>
#include <sstream>

// HTML embebido (definido al final de este archivo)
extern const char* PANEL_HTML;

// ── Utilidades JSON mínimas (sin dependencias) ───────────────────
static std::string JsonStr(const std::string& s) {
    return "\"" + s + "\"";
}
static std::string JsonBool(bool b) { return b ? "true" : "false"; }
static std::string JsonNum(uint64_t n) { return std::to_string(n); }

// ────────────────────────────────────────────────────────────────

WebUI::WebUI(uint16_t port) : port_(port) {
    detector_.Detect();
}

WebUI::~WebUI() { Stop(); }

bool WebUI::Start() {
    running_ = true;
    thread_  = std::thread(&WebUI::Serve, this);
    // Dar 200 ms al servidor para que esté listo antes de abrir el browser
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    OpenBrowser();
    return true;
}

void WebUI::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void WebUI::OpenBrowser() const {
    std::string url = "http://localhost:" + std::to_string(port_);
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOW);
}

// ── Servidor HTTP ────────────────────────────────────────────────

void WebUI::Serve() {
    httplib::Server svr;

    // GET / → panel HTML
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(PANEL_HTML, "text/html; charset=utf-8");
    });

    // GET /api/gpus → JSON con GPUs detectadas
    svr.Get("/api/gpus", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(BuildGPUsJson(), "application/json");
    });

    // GET /api/monitors → JSON con monitores disponibles
    svr.Get("/api/monitors", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(BuildMonitorsJson(), "application/json");
    });

    // GET /api/status → JSON con estado del stream
    svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(BuildStatusJson(), "application/json");
    });

    // POST /api/start → arranca el stream
    svr.Post("/api/start", [this](const httplib::Request& req, httplib::Response& res) {
        std::string result = ParseAndStart(req.body);
        res.set_content(result, "application/json");
    });

    // POST /api/stop → detiene el stream
    svr.Post("/api/stop", [this](const httplib::Request&, httplib::Response& res) {
        if (on_stop_) on_stop_();
        res.set_content(R"({"ok":true})", "application/json");
    });

    // POST /api/exit → cierra la aplicación completamente
    svr.Post("/api/exit", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true})", "application/json");
        if (on_stop_) on_stop_();
        std::exit(0);
    });

    // CORS para desarrollo local
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"}
    });

    std::printf("[WebUI] Panel en http://localhost:%u\n", port_);
    svr.listen("localhost", static_cast<int>(port_));
}

// ── Builders JSON ────────────────────────────────────────────────

std::string WebUI::BuildGPUsJson() const {
    const auto& gpus = detector_.GetAll();
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& g = gpus[i];
        std::string desc(g.description.begin(), g.description.end());

        const char* enc = "Auto";
        if (g.preferred_encoder == EncoderType::NVENC) enc = "NVENC";
        else if (g.preferred_encoder == EncoderType::AMF) enc = "AMF";
        else if (g.preferred_encoder == EncoderType::QSV) enc = "Quick Sync";

        ss << "{"
           << R"("index":)"   << g.adapter_index          << ","
           << R"("name":)"    << JsonStr(desc)             << ","
           << R"("encoder":)" << JsonStr(enc)              << ","
           << R"("vram":)"    << g.dedicated_vram_mb
           << "}";
        if (i + 1 < gpus.size()) ss << ",";
    }
    ss << "]";
    return ss.str();
}

std::string WebUI::BuildMonitorsJson() const {
    auto monitors = DXGICapture::EnumMonitors(0);
    std::ostringstream ss;
    ss << "[";
    UINT display_num = 1;
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];

        // Nombre amigable — evita barras del device name (\\.\DISPLAY1) en el JSON
        std::string label = "Monitor " + std::to_string(display_num++)
            + " — " + std::to_string(m.width) + "x" + std::to_string(m.height)
            + (m.is_primary ? " (principal)" : " (extendido)");

        ss << "{"
           << R"("output":)"  << m.output_index                    << ","
           << R"("adapter":)" << m.adapter_index                    << ","
           << R"("w":)"       << m.width                           << ","
           << R"("h":)"       << m.height                          << ","
           << R"("name":)"    << JsonStr(label)                     << ","
           << R"("primary":)" << (m.is_primary ? "true" : "false")
           << "}";
        if (i + 1 < monitors.size()) ss << ",";
    }
    ss << "]";
    return ss.str();
}

std::string WebUI::BuildStatusJson() const {
    StreamStatus st = on_status_ ? on_status_() : StreamStatus{};
    std::ostringstream ss;
    ss << "{"
       << R"("running":)"      << JsonBool(st.running)              << ","
       << R"("frames":)"       << JsonNum(st.frames_sent)           << ","
       << R"("encoder":)"      << JsonStr(st.encoder_name)          << ","
       << R"("tablet":)"       << JsonBool(st.tablet_ok)            << ","
       << R"("error":)"        << JsonStr(st.error_msg)
       << "}";
    return ss.str();
}

// ── Parseo JSON mínimo (evita dependencia de nlohmann/json) ─────
static std::string ExtractStr(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos) + 1;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) ++pos;
    size_t end = json.find_first_of("\",}", pos);
    return json.substr(pos, end - pos);
}
static int ExtractInt(const std::string& json, const std::string& key, int def = 0) {
    auto s = ExtractStr(json, key);
    return s.empty() ? def : std::stoi(s);
}
static bool ExtractBool(const std::string& json, const std::string& key, bool def = false) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos) + 1;
    while (pos < json.size() && json[pos] == ' ') ++pos;
    return json.substr(pos, 4) == "true";
}

std::string WebUI::ParseAndStart(const std::string& body) {
    if (!on_start_) return R"({"ok":false,"error":"No handler"})";

    StreamConfig cfg;
    cfg.target_ip    = ExtractStr (body, "ip");
    cfg.target_port  = static_cast<uint16_t>(ExtractInt(body, "port", 9000));
    cfg.width        = static_cast<uint32_t>(ExtractInt(body, "width",  1920));
    cfg.height       = static_cast<uint32_t>(ExtractInt(body, "height", 1080));
    cfg.fps          = static_cast<uint32_t>(ExtractInt(body, "fps",    60));
    cfg.bitrate_mbps = static_cast<uint32_t>(ExtractInt(body, "bitrate", 20));
    cfg.use_hevc     = ExtractBool(body, "hevc", true);

    cfg.monitor_index  = static_cast<uint32_t>(ExtractInt(body, "monitor",  0));
    cfg.adapter_index  = static_cast<uint32_t>(ExtractInt(body, "adapter",  0));

    std::string enc_str = ExtractStr(body, "encoder");
    if      (enc_str == "NVENC") cfg.encoder_type = EncoderType::NVENC;
    else if (enc_str == "AMF")   cfg.encoder_type = EncoderType::AMF;
    else if (enc_str == "QSV")   cfg.encoder_type = EncoderType::QSV;
    else                         cfg.encoder_type = EncoderType::Auto;

    if (cfg.target_ip.empty()) {
        return R"({"ok":false,"error":"IP de la tablet es requerida"})";
    }

    bool ok = on_start_(cfg);
    if (ok) return R"({"ok":true})";
    return R"({"ok":false,"error":"El encoder no pudo inicializarse. Verifica los drivers."})";
}

// ════════════════════════════════════════════════════════════════
// HTML EMBEBIDO — panel de control
// ════════════════════════════════════════════════════════════════
const char* PANEL_HTML = R"HTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ZeroLag Monitor — Control</title>
<style>
  :root {
    --bg:      #0f1117;
    --surface: #1a1d27;
    --border:  #2a2d3a;
    --accent:  #6c63ff;
    --accent2: #00d4aa;
    --text:    #e2e8f0;
    --muted:   #64748b;
    --danger:  #ef4444;
    --ok:      #22c55e;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Segoe UI', system-ui, sans-serif;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 2rem 1rem;
  }
  header {
    width: 100%;
    max-width: 720px;
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-bottom: 2rem;
  }
  header .logo {
    font-size: 1.6rem;
    font-weight: 700;
    letter-spacing: -0.5px;
    color: var(--accent);
  }
  header .logo span { color: var(--accent2); }
  .btn-exit {
    margin-left: auto;
    background: transparent;
    border: 1px solid var(--border);
    color: var(--muted);
    font-size: .8rem;
    padding: .35rem .9rem;
    border-radius: 7px;
  }
  .btn-exit:hover { border-color: var(--danger); color: var(--danger); }
  .pill {
    font-size: .7rem;
    padding: .2rem .6rem;
    border-radius: 999px;
    background: var(--surface);
    border: 1px solid var(--border);
    color: var(--muted);
  }
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 1.5rem;
    width: 100%;
    max-width: 720px;
    margin-bottom: 1rem;
  }
  .card h2 {
    font-size: .8rem;
    text-transform: uppercase;
    letter-spacing: .08em;
    color: var(--muted);
    margin-bottom: 1rem;
  }
  /* GPU list */
  .gpu-list { display: flex; flex-direction: column; gap: .5rem; }
  .gpu-item {
    display: flex;
    align-items: center;
    gap: .75rem;
    padding: .75rem 1rem;
    border-radius: 8px;
    border: 1px solid var(--border);
    cursor: pointer;
    transition: border-color .15s;
  }
  .gpu-item:hover { border-color: var(--accent); }
  .gpu-item.selected { border-color: var(--accent); background: #6c63ff14; }
  .gpu-item input[type=radio] { accent-color: var(--accent); }
  .gpu-name { font-weight: 600; font-size: .95rem; }
  .gpu-meta { font-size: .75rem; color: var(--muted); margin-top: .1rem; }
  .enc-badge {
    margin-left: auto;
    font-size: .7rem;
    padding: .2rem .55rem;
    border-radius: 6px;
    font-weight: 600;
  }
  .enc-NVENC    { background:#76b90022; color:#76b900; border:1px solid #76b90044; }
  .enc-AMF      { background:#ed1c2422; color:#ed1c24; border:1px solid #ed1c2444; }
  .enc-QSV      { background:#0071c522; color:#0071c5; border:1px solid #0071c544; }
  .enc-Auto     { background:#6c63ff22; color:#6c63ff; border:1px solid #6c63ff44; }
  /* Form grid */
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: .75rem; }
  .field { display: flex; flex-direction: column; gap: .35rem; }
  .field.full { grid-column: 1 / -1; }
  label { font-size: .75rem; color: var(--muted); text-transform: uppercase; letter-spacing: .05em; }
  input, select {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 7px;
    color: var(--text);
    padding: .55rem .75rem;
    font-size: .9rem;
    transition: border-color .15s;
    width: 100%;
  }
  input:focus, select:focus {
    outline: none;
    border-color: var(--accent);
  }
  select option { background: var(--bg); }
  /* Toggle HEVC/H264 */
  .toggle-row {
    display: flex;
    align-items: center;
    gap: .75rem;
    font-size: .85rem;
  }
  .toggle {
    position: relative;
    width: 42px; height: 24px;
  }
  .toggle input { display: none; }
  .slider {
    position: absolute; inset: 0;
    background: var(--border);
    border-radius: 24px;
    cursor: pointer;
    transition: background .2s;
  }
  .slider:before {
    content: '';
    position: absolute;
    width: 18px; height: 18px;
    top: 3px; left: 3px;
    background: white;
    border-radius: 50%;
    transition: transform .2s;
  }
  .toggle input:checked + .slider { background: var(--accent); }
  .toggle input:checked + .slider:before { transform: translateX(18px); }
  /* Action row */
  .action-row {
    display: flex;
    gap: .75rem;
    align-items: center;
    margin-top: .25rem;
  }
  button {
    padding: .65rem 1.5rem;
    border-radius: 8px;
    font-size: .9rem;
    font-weight: 600;
    cursor: pointer;
    border: none;
    transition: opacity .15s, transform .1s;
  }
  button:active { transform: scale(.97); }
  .btn-main {
    flex: 1;
    background: var(--accent);
    color: white;
  }
  .btn-main.stopping { background: var(--danger); }
  button:disabled { opacity: .4; cursor: not-allowed; }
  /* Status bar */
  .status-bar {
    display: flex;
    align-items: center;
    gap: .6rem;
    font-size: .85rem;
    color: var(--muted);
  }
  .dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--muted);
    flex-shrink: 0;
  }
  .dot.live { background: var(--ok); box-shadow: 0 0 6px var(--ok); animation: pulse 2s infinite; }
  @keyframes pulse {
    0%,100% { opacity: 1; }
    50% { opacity: .4; }
  }
  .error-msg {
    color: var(--danger);
    font-size: .82rem;
    margin-top: .5rem;
    display: none;
  }
  .frames-counter { font-variant-numeric: tabular-nums; }
</style>
</head>)HTML"
R"HTML(<body>

<header>
  <div class="logo">ZeroLag<span> Monitor</span></div>
  <div class="pill">Windows Host</div>
  <button class="btn-exit" onclick="exitApp()">✕ Cerrar</button>
</header>

<!-- GPU Selection -->
<div class="card">
  <h2>GPU / Encoder</h2>
  <div class="gpu-list" id="gpuList">
    <div style="color:var(--muted);font-size:.85rem">Detectando GPUs...</div>
  </div>
</div>

<!-- Monitor Selection -->
<div class="card">
  <h2>Monitor a Capturar</h2>
  <div class="gpu-list" id="monitorList">
    <div style="color:var(--muted);font-size:.85rem">Detectando monitores...</div>
  </div>
  <div id="monitorRes" style="margin-top:.6rem;font-size:.8rem;color:var(--accent2)"></div>
  <div style="margin-top:.4rem;font-size:.75rem;color:var(--muted)">
    Para monitor extendido: instala un <a href="#" onclick="return false;"
      title="Descarga Virtual Display Driver desde GitHub: itsmikethetech/Virtual-Display-Driver"
      style="color:var(--accent2);text-decoration:none"
      onmouseover="this.style.textDecoration='underline'"
      onmouseout="this.style.textDecoration='none'">driver de pantalla virtual</a>,
    configura "Extender" en Configuración → Pantalla, y selecciona el monitor 2.
  </div>
</div>

<!-- Stream Config -->
<div class="card">
  <h2>Configuración</h2>
  <div class="grid">
    <div class="field full">
      <label>Conexión</label>
      <div class="toggle-row">
        <span style="font-weight:600">USB-C</span>
        <label class="toggle">
          <input type="checkbox" id="useWifi" onchange="onModeChange()" />
          <div class="slider"></div>
        </label>
        <span style="color:var(--muted)">WiFi</span>
        <span id="usbHint" style="margin-left:auto;font-size:.75rem;color:var(--accent2)">
          ADB tunnel — sin IP requerida
        </span>
      </div>
    </div>
    <div class="field full" id="ipRow" style="display:none">
      <label>IP de la Tablet Android</label>
      <input id="ip" type="text" placeholder="192.168.1.X" />
    </div>
    <div class="field">
      <label>Puerto</label>
      <input id="port" type="number" value="9000" min="1024" max="65535" />
    </div>
    <div class="field">
      <label>Codec</label>
      <div class="toggle-row">
        <span>H.264</span>
        <label class="toggle">
          <input type="checkbox" id="hevc" checked />
          <div class="slider"></div>
        </label>
        <span>H.265</span>
      </div>
    </div>
    <div class="field">
      <label>Resolución</label>
      <select id="resolution">
        <option value="1920x1080" selected>1080p — 1920×1080</option>
        <option value="2560x1440">1440p — 2560×1440</option>
        <option value="3840x2160">4K — 3840×2160</option>
        <option value="1280x720">720p — 1280×720</option>
      </select>
    </div>
    <div class="field">
      <label>FPS</label>
      <select id="fps">
        <option value="60" selected>60 fps</option>
        <option value="30">30 fps</option>
        <option value="120">120 fps</option>
      </select>
    </div>
    <div class="field">
      <label>Bitrate</label>
      <select id="bitrate">
        <option value="10">10 Mbps</option>
        <option value="20" selected>20 Mbps</option>
        <option value="30">30 Mbps</option>
        <option value="50">50 Mbps</option>
      </select>
    </div>
  </div>
  <div class="error-msg" id="errMsg"></div>
</div>

<!-- Controls + Status -->
<div class="card">
  <div class="status-bar" style="margin-bottom:1rem">
    <div class="dot" id="tabletDot"></div>
    <span id="tabletText" style="font-size:.85rem;color:var(--muted)">Buscando tablet...</span>
  </div>
  <div class="action-row">
    <button class="btn-main" id="btnMain" onclick="toggleStream()">▶ Iniciar Stream</button>
  </div>
  <div class="status-bar" style="margin-top:1rem">
    <div class="dot" id="dot"></div>
    <span id="statusText">Inactivo</span>
    <span class="frames-counter" id="frames" style="margin-left:auto;display:none"></span>
  </div>
</div>

<script>
let selectedEncoder  = 'Auto';
let selectedMonitor  = 0;
let selectedAdapter  = 0;
let selectedMonitorW = 1920;
let selectedMonitorH = 1080;
let isStreaming       = false;

// ── Toggle USB / WiFi ────────────────────────────────────────────
function onModeChange() {
  const wifi = document.getElementById('useWifi').checked;
  document.getElementById('ipRow').style.display  = wifi ? '' : 'none';
  document.getElementById('usbHint').style.display = wifi ? 'none' : '';
}

// ── Cargar GPUs al abrir ─────────────────────────────────────────
async function loadGPUs() {
  const res  = await fetch('/api/gpus');
  const gpus = await res.json();
  const list = document.getElementById('gpuList');
  list.innerHTML = '';

  list.appendChild(makeGPUItem({
    index: -1, name: 'Auto (recomendado)', encoder: 'Auto', vram: 0
  }, true));

  gpus.forEach(g => list.appendChild(makeGPUItem(g, false)));
}

function makeGPUItem(g, checked) {
  const div    = document.createElement('div');
  const encCls = 'enc-' + g.encoder.replace(' ', '');
  div.className = 'gpu-item' + (checked ? ' selected' : '');
  div.innerHTML = `
    <input type="radio" name="gpu" value="${g.encoder}" ${checked ? 'checked' : ''}>
    <div>
      <div class="gpu-name">${g.name}</div>
      ${g.vram > 0 ? `<div class="gpu-meta">${g.vram} MB VRAM</div>` : ''}
    </div>
    <div class="enc-badge ${encCls}">${g.encoder}</div>
  `;
  div.addEventListener('click', () => {
    document.querySelectorAll('.gpu-item').forEach(el => el.classList.remove('selected'));
    div.classList.add('selected');
    div.querySelector('input').checked = true;
    selectedEncoder = g.encoder;
  });
  return div;
}

// ── Cargar monitores ─────────────────────────────────────────────
async function loadMonitors() {
  const list = document.getElementById('monitorList');
  try {
    const res      = await fetch('/api/monitors');
    const monitors = await res.json();
    list.innerHTML = '';

    if (monitors.length === 0) {
      list.innerHTML = '<div style="color:var(--muted);font-size:.85rem">Solo 1 monitor detectado. Instalá el driver virtual para agregar un segundo.</div>';
      return;
    }
    monitors.forEach((m, i) => list.appendChild(makeMonitorItem(m, i === 0)));
  } catch(e) {
    list.innerHTML = '<div style="color:var(--danger);font-size:.82rem">Error al detectar monitores: ' + e.message + '</div>';
  }
}

function selectMonitor(m) {
  selectedMonitor  = m.output;
  selectedAdapter  = m.adapter;
  selectedMonitorW = m.w;
  selectedMonitorH = m.h;
  document.getElementById('monitorRes').textContent =
    `Resolución de captura: ${m.w}×${m.h}`;
}

function makeMonitorItem(m, checked) {
  if (checked) selectMonitor(m);
  const div = document.createElement('div');
  div.className = 'gpu-item' + (checked ? ' selected' : '');
  const badge = m.primary
    ? '<div class="enc-badge enc-AMF">Principal</div>'
    : '<div class="enc-badge enc-Auto">Extendido</div>';
  div.innerHTML = `
    <input type="radio" name="monitor" value="${m.output}" ${checked ? 'checked' : ''}>
    <div><div class="gpu-name">${m.name}</div></div>
    ${badge}
  `;
  div.addEventListener('click', () => {
    document.querySelectorAll('#monitorList .gpu-item').forEach(el => el.classList.remove('selected'));
    div.classList.add('selected');
    div.querySelector('input').checked = true;
    selectMonitor(m);
  });
  return div;
}

// ── Start / Stop ─────────────────────────────────────────────────
async function toggleStream() {
  isStreaming ? await doStop() : await doStart();
}

async function doStart() {
  const errDiv = document.getElementById('errMsg');
  errDiv.style.display = 'none';

  const wifi = document.getElementById('useWifi').checked;
  const ip   = wifi ? document.getElementById('ip').value.trim() : '127.0.0.1';
  if (wifi && !ip) { showError('Ingresa la IP de la tablet Android.'); return; }

  const w       = selectedMonitorW;
  const h       = selectedMonitorH;
  const fps     = parseInt(document.getElementById('fps').value);
  const bitrate = parseInt(document.getElementById('bitrate').value);
  const hevc    = document.getElementById('hevc').checked;

  setUI('starting');
  const res  = await fetch('/api/start', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      encoder: selectedEncoder, monitor: selectedMonitor, adapter: selectedAdapter,
      ip, port: parseInt(document.getElementById('port').value),
      width: w, height: h, fps, bitrate, hevc
    })
  });
  const data = await res.json();
  if (data.ok) { isStreaming = true; setUI('running'); }
  else         { setUI('idle'); showError(data.error || 'Error desconocido.'); }
}

async function doStop() {
  await fetch('/api/stop', { method: 'POST' });
  isStreaming = false;
  setUI('idle');
}

async function exitApp() {
  if (!confirm('¿Cerrar ZeroLag Monitor?')) return;
  await fetch('/api/exit', { method: 'POST' }).catch(() => {});
  window.close();
}

// ── Polling de estado ────────────────────────────────────────────
async function pollStatus() {
  try {
    const res  = await fetch('/api/status');
    const data = await res.json();

    // Estado de la tablet (ADB forward)
    const tabletDot  = document.getElementById('tabletDot');
    const tabletText = document.getElementById('tabletText');
    if (data.tablet) {
      tabletDot.classList.add('live');
      tabletText.textContent = '● Tablet conectada';
      tabletText.style.color = 'var(--ok)';
    } else {
      tabletDot.classList.remove('live');
      tabletText.textContent = 'Esperando tablet — conectá el cable USB-C';
      tabletText.style.color = 'var(--muted)';
    }

    // Si el backend dejó de correr pero la UI cree que sí → sincronizar
    if (!data.running && isStreaming) { isStreaming = false; setUI('idle'); return; }
    if (!data.running) return;
    document.getElementById('frames').textContent =
      `${data.encoder}  ·  ${data.frames.toLocaleString()} frames`;
  } catch(_) {}
}

// ── UI helpers ───────────────────────────────────────────────────
function setUI(state) {
  const btn    = document.getElementById('btnMain');
  const dot    = document.getElementById('dot');
  const status = document.getElementById('statusText');
  const frames = document.getElementById('frames');

  if (state === 'idle') {
    btn.textContent = '▶ Iniciar Stream';
    btn.classList.remove('stopping');
    btn.disabled = false;
    dot.classList.remove('live');
    status.textContent = 'Inactivo';
    frames.style.display = 'none';
  } else if (state === 'starting') {
    btn.disabled = true;
    btn.textContent = 'Iniciando...';
    status.textContent = 'Iniciando encoder...';
  } else if (state === 'running') {
    btn.textContent = '■ Detener';
    btn.classList.add('stopping');
    btn.disabled = false;
    dot.classList.add('live');
    status.textContent = 'En vivo';
    frames.style.display = '';
  }
}

function showError(msg) {
  const el = document.getElementById('errMsg');
  el.textContent = msg;
  el.style.display = 'block';
}

// ── Init ─────────────────────────────────────────────────────────
loadGPUs();
loadMonitors();
setInterval(pollStatus, 1500);  // polling único para tablet + stream
</script>
</body>
</html>)HTML";
