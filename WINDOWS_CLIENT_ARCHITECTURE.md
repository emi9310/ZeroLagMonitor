# 🖥️ Windows Client Architecture - ZeroLagMonitor

## 📋 Tabla de Contenidos
1. [Visión General](#visión-general)
2. [Arquitectura Actual del Proyecto](#arquitectura-actual-del-proyecto)
3. [Nueva Arquitectura: Windows Client](#nueva-arquitectura-windows-client)
4. [Componentes Detallados](#componentes-detallados)
5. [Flujo de Datos](#flujo-de-datos)
6. [Estructura de Directorios](#estructura-de-directorios)
7. [Protocolo de Comunicación](#protocolo-de-comunicación)
8. [Modos de Conectividad](#modos-de-conectividad)
9. [Proceso de Compilación](#proceso-de-compilación)
10. [Plan de Implementación](#plan-de-implementación)
11. [Verificación y Testing](#verificación-y-testing)

---

## Visión General

**ZeroLagMonitor** es una solución de streaming de pantalla de **ultra baja latencia**. Permite transmitir el contenido de una pantalla de un Windows host a dispositivos clientes (Android, y próximamente Windows).

### Objetivo del Windows Client

Crear un cliente nativo de Windows que pueda recibir y mostrar el stream de video del host en **tiempo real** con mínima latencia. Esto permite usar un **segundo PC como monitor remoto** mediante:
- 🔌 **USB-C directo** (vía ADB tunnel)
- 🌐 **LAN local** (Ethernet/WiFi)
- 🔗 **Ethernet directo entre dos NICs**

---

## Arquitectura Actual del Proyecto

### 🏗️ Componentes Existentes

```
┌─────────────────────────────────────────────────────────┐
│  Windows Host (Servidor)                                │
│  ┌────────────────────────────────────────────────────┐ │
│  │ • DXGI Desktop Duplication → Captura GPU           │ │
│  │ • Hardware Encoders (NVENC/AMF/QSV/MF)            │ │
│  │ • AsyncSender TCP/UDP → Transmisión               │ │
│  │ • WebUI Panel de control (puerto 8080)            │ │
│  └────────────────────────────────────────────────────┘ │
└──────────┬──────────────────────────────────────────────┘
           │ Stream H.264/H.265
           │ Protocolo: MYRR (header 16B + payload)
           │ Puerto: 9000 (TCP o UDP)
           │
┌──────────▼──────────────────────────────────────────────┐
│  Android Client (Receptor)                              │
│  ┌────────────────────────────────────────────────────┐ │
│  │ • TCP/UDP Receiver                                 │ │
│  │ • MediaCodec Decodificador H.264/H.265            │ │
│  │ • SurfaceView Renderizado                         │ │
│  │ • Aplicación nativa Kotlin + C++/JNI              │ │
│  └────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

### Protocolo MYRR Actual

Cada paquete tiene estructura:

```
┌──────────────────────────────────────────┐
│ Header (16 bytes)                        │
├──────────────────────────────────────────┤
│ Magic:     0x4D595252 ("MYRR")  [4B]    │
│ Size:      Bytes del payload     [4B]    │
│ PTS:       Timestamp (μs)        [8B]    │
├──────────────────────────────────────────┤
│ Payload (variable, máx 1 MB)             │
│ • NAL units H.264 o H.265 crudos         │
└──────────────────────────────────────────┘
```

---

## Nueva Arquitectura: Windows Client

### 🎯 Objetivo

Crear un **cliente Windows** que se ejecute en una segundo PC y actúe como **monitor remoto nativo**.

### 📐 Diagrama Completo

```
┌─────────────────────────────────────────────────────────────────┐
│  Windows Host (EXISTENTE)                                       │
│  • DXGI Capture → Encode → AsyncSender TCP/UDP:9000            │
└────────────────────┬────────────────────────────────────────────┘
                     │ Stream [MYRR Header + NAL units]
                     │ TCP (127.0.0.1:9000 via ADB) o UDP (192.168.x.x:9000)
                     │
     ┌───────────────┼───────────────┐
     │               │               │
     ▼               ▼               ▼
 ┌─────────┐   ┌──────────┐   ┌──────────────┐
 │  USB-C  │   │   LAN    │   │  Ethernet    │
 │  (ADB)  │   │ (WiFi)   │   │  Directo     │
 └────┬────┘   └────┬─────┘   └────┬─────────┘
      │             │              │
      └─────────────┼──────────────┘
                    │
    ┌───────────────▼──────────────────────────────────────┐
    │  Windows Client (NUEVO) - puerto 9000               │
    │  ┌────────────────────────────────────────────────┐  │
    │  │ ┌──────────────────────────────────────────┐   │  │
    │  │ │ Receiver (network/)                      │   │  │
    │  │ │ • Escucha TCP en 0.0.0.0:9000           │   │  │
    │  │ │ • O conecta a server remoto              │   │  │
    │  │ │ • Valida header MYRR                     │   │  │
    │  │ │ • Ring buffer lock-free SPSC             │   │  │
    │  │ └──────────────────────────────────────────┘   │  │
    │  │              ↓ NAL units                        │  │
    │  │ ┌──────────────────────────────────────────┐   │  │
    │  │ │ MediaFoundationDecoder (decode/)         │   │  │
    │  │ │ • IMFSourceReader + H.264/H.265 decoder │   │  │
    │  │ │ • Hardware-acelerado (si GPU disponible)│   │  │
    │  │ │ • Output: IMFSample (frames NV12)       │   │  │
    │  │ └──────────────────────────────────────────┘   │  │
    │  │              ↓ Frames RGB/NV12                 │  │
    │  │ ┌──────────────────────────────────────────┐   │  │
    │  │ │ DirectXRenderer (render/)                │   │  │
    │  │ │ • Ventana DirectX 11 fullscreen         │   │  │
    │  │ │ • Shader HLSL NV12 → RGB conversion    │   │  │
    │  │ │ • GPU rendering + vsync                 │   │  │
    │  │ └──────────────────────────────────────────┘   │  │
    │  │              ↓                                  │  │
    │  │ ┌──────────────────────────────────────────┐   │  │
    │  │ │ WebUI (ui/) - puerto 8081               │   │  │
    │  │ │ • Panel de control HTTP                 │   │  │
    │  │ │ • Conexión/Desconexión                  │   │  │
    │  │ │ • Estadísticas (FPS, latencia)          │   │  │
    │  │ └──────────────────────────────────────────┘   │  │
    │  └────────────────────────────────────────────────┘  │
    └─────────────────────────────────────────────────────┘
                        ↓
            ┌───────────────────────┐
            │  Monitor Remoto       │
            │  (Pantalla virtual)   │
            │  Fullscreen DirectX   │
            └───────────────────────┘
```

---

## Componentes Detallados

### 1. **Receiver** (`windows_client/network/`)

**Responsabilidad**: Recibir paquetes MYRR del servidor remoto.

#### Características:
- ✅ Escucha TCP en puerto 9000 (modo servidor local o ADB)
- ✅ O conecta a servidor remoto TCP (modo cliente para LAN)
- ✅ Valida magic `0x4D595252` en cada header
- ✅ Almacena NAL units en ring buffer SPSC lock-free
- ✅ Ejecuta en thread separado para no bloquear decodificador

#### Interfaz:
```cpp
class IReceiver {
    virtual void Start(std::string server_ip, uint16_t port) = 0;
    virtual void Stop() = 0;
    virtual bool GetNextNAL(std::vector<uint8_t>& nal_data, 
                            uint64_t& pts_us) = 0;
    virtual bool IsConnected() const = 0;
    virtual ConnectionStats GetStats() const = 0;
};

class TCPReceiver : public IReceiver {
    // Escucha o conecta por TCP
};

class UDPReceiver : public IReceiver {
    // Escucha UDP (futuro)
};
```

#### Implementación:
```cpp
// Ejemplo de uso
auto receiver = std::make_unique<TCPReceiver>();
receiver->Start("127.0.0.1", 9000);  // Escucha local con ADB

while (receiver->IsConnected()) {
    std::vector<uint8_t> nal;
    uint64_t pts;
    if (receiver->GetNextNAL(nal, pts)) {
        // Procesar NAL unit
        decoder->DecodeNAL(nal, pts);
    }
}
```

---

### 2. **MediaFoundationDecoder** (`windows_client/decode/`)

**Responsabilidad**: Decodificar NAL units H.264/H.265 a frames de video.

#### Características:
- ✅ Usa IMFSourceReader (API Media Foundation)
- ✅ Hardware-acelerado (GPU decodificador si disponible)
- ✅ Configurable: resolución, frame rate, codec
- ✅ Salida: IMFSample con formato NV12 (mejor para rendering GPU)
- ✅ Manejo de errores: skip frames inválidos

#### Interfaz:
```cpp
class IDecoder {
    virtual void Initialize(int width, int height, bool hevc) = 0;
    virtual IMFSample* DecodeNAL(const uint8_t* data, 
                                 size_t size, 
                                 uint64_t pts_us) = 0;
    virtual void Flush() = 0;
};

class MediaFoundationDecoder : public IDecoder {
    // Usa IMFSourceReader
};
```

#### Configuración:
```cpp
decoder->Initialize(1920, 1080, false);  // H.264, 1920x1080

// Recibe cada NAL unit
IMFSample* frame = decoder->DecodeNAL(nal_data, nal_size, pts);
if (frame) {
    renderer->RenderFrame(frame);
}
```

#### Dependencias:
```cmake
# CMakeLists.txt
target_link_libraries(windows_client PUBLIC 
    mf.lib        # Media Foundation
    mfuuid.lib    # Media Foundation UUIDs
    ole32.lib     # COM initialization
)
```

---

### 3. **DirectXRenderer** (`windows_client/render/`)

**Responsabilidad**: Renderizar frames decodificados a pantalla con ultra baja latencia.

#### Características:
- ✅ Ventana HWND fullscreen 1920×1080
- ✅ Device D3D11 + DXGI Swap Chain 1.1
- ✅ Shader pixel HLSL para convertir NV12 → RGB en GPU
- ✅ Sincronización vsync (60 FPS)
- ✅ Medición de latencia frame-to-display

#### Interfaz:
```cpp
class DirectXRenderer {
    void Initialize(HWND hwnd, int width, int height);
    void RenderFrame(IMFSample* sample);
    void Present();
    void Resize(int new_width, int new_height);
    LatencyStats GetLatencyStats() const;
};
```

#### Arquitectura Interna:

**Vertices (Fullscreen Quad)**:
```
(-1, +1) ─────────── (+1, +1)
   │                    │
   │     [Screen]       │
   │                    │
(-1, -1) ─────────── (+1, -1)
```

**Shader NV12 → RGB** (`render/Shaders/ps_nv12.hlsl`):
```hlsl
// Input: Y plane (R channel), UV planes (GR channels)
// Output: RGB color

float3 ConvertNV12toRGB(float y_val, float u_val, float v_val) {
    // Fórmula estándar YUV → RGB (BT.709)
    float r = y_val + 1.402 * (v_val - 0.5);
    float g = y_val - 0.344 * (u_val - 0.5) - 0.714 * (v_val - 0.5);
    float b = y_val + 1.772 * (u_val - 0.5);
    return float3(r, g, b);
}
```

#### Presentación:
```cpp
// Swap Chain en modo flip
swapChain->Present(1, 0);  // 1 = vsync, 0 = no flags
```

---

### 4. **Client** (`windows_client/src/Client.hpp`)

**Responsabilidad**: Orquestador principal que conecta Receiver → Decoder → Renderer.

#### Arquitectura de Threading:

```
┌─────────────────────────────────────────────────────┐
│ Main Thread                                         │
│ • Procesa mensajes de ventana (HWND)               │
│ • Maneja WebUI requests                            │
│ • Loop principal del juego / aplicación            │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Receiver Thread                                     │
│ • Lee socket TCP                                   │
│ • Valida paquetes MYRR                             │
│ • Enqueue NAL en ring buffer                       │
│ • Reconexión automática si falla                   │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Decoder Thread                                      │
│ • Dequeue NAL del ring buffer                      │
│ • Decodifica con Media Foundation                  │
│ • Enqueue frame en buffer de frames                │
│ • Mide latencia de decodificación                  │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Renderer Thread (Main Thread)                       │
│ • Dequeue frame decodificado                       │
│ • Renderiza a swap chain DirectX                   │
│ • Presenta a monitor con vsync                     │
│ • Mide latencia total (arrival → display)          │
└─────────────────────────────────────────────────────┘
```

#### Interfaz:
```cpp
class Client {
    Client();
    ~Client();
    
    void Initialize(const ClientConfig& config);
    void Connect(const std::string& server_ip, uint16_t port);
    void Disconnect();
    void ProcessFrame();  // Llamado cada vsync
    
    bool IsConnected() const;
    ClientStats GetStats() const;
};
```

#### Loop Principal:
```cpp
Client client;
client.Initialize(config);
client.Connect("192.168.1.50", 9000);

while (client.IsConnected() && !should_exit) {
    client.ProcessFrame();  // Renderiza 1 frame (60 FPS)
    HandleMessages();       // Procesa eventos Windows
}

client.Disconnect();
```

---

### 5. **WebUI** (`windows_client/ui/WebUI.hpp`)

**Responsabilidad**: Panel HTTP de control y estadísticas.

#### Endpoints REST:

| Método | Endpoint | Función | Respuesta |
|--------|----------|---------|-----------|
| GET | `/` | Sirve HTML panel | `text/html` |
| GET | `/api/status` | Estado del stream | JSON |
| POST | `/api/connect` | Conecta a servidor | `{status: "connected"}` |
| POST | `/api/disconnect` | Desconecta | `{status: "disconnected"}` |
| POST | `/api/exit` | Cierra aplicación | `{status: "exiting"}` |

#### Respuesta JSON - `/api/status`:
```json
{
  "connected": true,
  "server_ip": "192.168.1.50",
  "server_port": 9000,
  "frames_received": 1234,
  "frames_dropped": 2,
  "fps": 59.8,
  "latency_ms": {
    "network": 1.2,
    "decode": 3.5,
    "render": 0.8,
    "total": 5.5
  },
  "resolution": "1920x1080",
  "decoder_type": "MediaFoundation H.264",
  "uptime_seconds": 3600
}
```

#### Configuración POST - `/api/connect`:
```json
{
  "server_ip": "192.168.1.50",
  "port": 9000,
  "use_adb_forward": false,
  "use_hevc": false
}
```

---

### 6. **Configuration** (`windows_client/config/`)

**Config JSON** (`config.json`):
```json
{
  "server_ip": "127.0.0.1",
  "port": 9000,
  "use_adb_forward": true,
  "adb_path": "adb.exe",
  
  "decoder": "auto",
  "fullscreen": true,
  "width": 1920,
  "height": 1080,
  "vsync": true,
  
  "webui_port": 8081,
  "webui_enabled": true,
  
  "logging": {
    "level": "info",
    "file": "myrror_client.log"
  }
}
```

**Command-line Args**:
```bash
# Modo 1: USB-C con ADB
windows_client.exe --adb-forward

# Modo 2: LAN remota
windows_client.exe --server-ip 192.168.1.50 --port 9000

# Modo 3: Custom config
windows_client.exe --config custom_config.json --fullscreen
```

---

## Flujo de Datos

### End-to-End: Pixel → Pantalla

```
1. Windows Host (Server)
   └─ DXGI Capture (GPU)
      └─ Encoder H.264 (GPU)
         └─ [MYRR Header 16B + NAL unit 100KB]
            └─ TCP send o UDP send

2. Red
   └─ [Paquete MYRR] → ADB tunnel o Ethernet

3. Windows Client (Receiver)
   └─ TCP/UDP recv socket
      └─ Valida magic 0x4D595252
         └─ Enqueue NAL en ring buffer SPSC

4. Decoder Thread
   └─ Dequeue NAL
      └─ IMFSourceReader
         └─ Hardware H.264 decoder (si disponible)
            └─ IMFSample NV12 (decodificado)
               └─ Enqueue en frame buffer

5. Renderer Thread (Main)
   └─ Dequeue frame IMFSample
      └─ Lock D3D11 texture NV12
         └─ Pixel shader: NV12 → RGB conversion
            └─ Draw fullscreen quad
               └─ swapChain->Present(1, 0)  ← vsync
                  └─ ¡Pixel en pantalla!

Latencia Total ≈ 5-15 ms (red + decodificador + render)
```

---

## Estructura de Directorios

```
windows_client/
├── CMakeLists.txt                    # Build system
├── config.json                       # Configuración por defecto
│
├── src/
│   ├── main.cpp                      # Punto de entrada
│   ├── Client.hpp                    # Orquestador principal
│   ├── Client.cpp
│   │
│   └── ui/
│       ├── Window.hpp                # Ventana HWND
│       ├── Window.cpp
│       ├── WebUI.hpp                 # Servidor HTTP
│       └── WebUI.cpp
│
├── network/
│   ├── Receiver.hpp                  # Interfaz receptor
│   ├── TCPReceiver.hpp/cpp           # Implementación TCP
│   ├── UDPReceiver.hpp/cpp           # Implementación UDP (futuro)
│   ├── AsyncReceiver.hpp             # Ring buffer SPSC
│   └── PacketStructure.hpp           # Header MYRR (16 bytes)
│
├── decode/
│   ├── IDecoder.hpp                  # Interfaz decodificador
│   ├── MediaFoundationDecoder.hpp    # Implementación MF
│   ├── MediaFoundationDecoder.cpp
│   ├── H264Decoder.hpp/cpp           # Wrapper H.264
│   └── DecoderFactory.hpp            # Factory para crear decodificador
│
├── render/
│   ├── DirectXRenderer.hpp           # Renderer principal
│   ├── DirectXRenderer.cpp
│   ├── FrameBuffer.hpp               # Buffer de frames
│   │
│   └── Shaders/
│       ├── vs_screen.hlsl            # Vertex shader (fullscreen quad)
│       ├── ps_nv12.hlsl              # Pixel shader (NV12 → RGB)
│       └── ShaderCompiler.hpp        # Compilador HLSL en runtime
│
├── config/
│   ├── Config.hpp                    # Estructura de configuración
│   ├── Config.cpp
│   └── ConfigLoader.hpp/cpp          # Cargar JSON + command-line
│
├── util/
│   ├── Logger.hpp                    # Sistema de logging
│   ├── Logger.cpp
│   ├── SPSCRingBuffer.hpp            # Ring buffer lock-free
│   ├── Timing.hpp                    # Medición de latencia
│   ├── HResultError.hpp              # Manejo de HRESULT COM
│   └── StopWatch.hpp                 # Timer de alta resolución
│
├── third_party/
│   ├── httplib.h                     # Servidor HTTP (header-only)
│   └── SPSCRingBuffer.hpp            # (Copiar del host)
│
└── tests/
    ├── test_receiver.cpp             # Unit tests receptor
    ├── test_decoder.cpp              # Unit tests decodificador
    └── integration_test.cpp          # Test E2E
```

---

## Protocolo de Comunicación

### Formato de Paquete MYRR

Cada paquete transmitido por el host tiene **16 bytes de header + payload variable**:

```
BYTE OFFSET  FIELD       TYPE      SIZE  DESCRIPCIÓN
───────────  ───────────  ────────  ────  ───────────────────────────────
0            magic       uint32_t   4     0x4D595252 ("MYRR" en ASCII)
4            size        uint32_t   4     Tamaño del payload (en bytes)
8            pts_us      uint64_t   8     Timestamp PTS (microsegundos)
────────────────────────────────────────────────────────────────────────
16           [payload]   uint8_t[]  var   NAL units H.264 o H.265 crudo
```

### Ejemplo en Hexadecimal:

```
Header:
52 4D 59 52    (0x4D595252 = "MYRR" little-endian)
E8 03 00 00    (1000 bytes de payload)
00 01 22 00 00 00 00 00  (PTS = 1234567 microsegundos)

Payload (primeros bytes):
00 00 00 01 67 64 00 ...  (NAL unit H.264 con SPS)
```

### Validación en Receiver:

```cpp
// Lectura de header
uint32_t magic;
socket.recv(&magic, 4);

if (magic != 0x4D595252) {
    LOG_ERROR("Paquete inválido, magic incorrecto");
    return false;
}

uint32_t size;
socket.recv(&size, 4);

uint64_t pts_us;
socket.recv(&pts_us, 8);

// Leer payload
std::vector<uint8_t> nal_data(size);
socket.recv(nal_data.data(), size);
```

---

## Modos de Conectividad

### Opción 1: USB-C (ADB Tunnel) ⚡ **Recomendado**

**Uso**: Mismo usuario, máxima simplicidad.

```bash
# En terminal Windows:
adb forward tcp:9000 tcp:9000

# Luego ejecutar client:
windows_client.exe --adb-forward
```

**Ventajas**:
- ✅ No necesita IP del servidor (ADB lo resuelve automáticamente)
- ✅ Seguro (aislado en USB)
- ✅ Automatizable (el client busca adb.exe)

**Latencia**: 1-3 ms (USB 3.0+)

---

### Opción 2: LAN Local (Ethernet/WiFi)

**Uso**: Mismo PC del usuario o red local.

```bash
# Averiguar IP del servidor:
ipconfig  # En Windows

# Ejecutar client con IP conocida:
windows_client.exe --server-ip 192.168.1.50 --port 9000
```

**Ventajas**:
- ✅ Funciona sin ADB
- ✅ Reutilizable entre sesiones (no necesita recablear)
- ✅ Soporta múltiples clientes simultáneamente

**Latencia**: 3-10 ms (LAN local)

---

### Opción 3: Ethernet Directo (Punto a Punto)

**Uso**: Máxima latencia baja, conexión física directa.

```
[PC A - NICs]
    ↓
  [Cable Ethernet corto]
    ↓
[PC B - NICs]
```

**Configuración de red (manual)**:

PC A (Server):
```
IP: 192.168.100.1
Mask: 255.255.255.0
Gateway: (ninguno)
```

PC B (Client):
```
IP: 192.168.100.2
Mask: 255.255.255.0
Gateway: (ninguno)
```

Ejecutar:
```bash
windows_client.exe --server-ip 192.168.100.1 --port 9000
```

**Ventajas**:
- ✅ Latencia mínima (< 1 ms)
- ✅ Ancho de banda dedicado (no compartido)
- ✅ Aislado de otros tráfico de red

**Latencia**: < 1-2 ms (cable directo)

---

## Proceso de Compilación

### Requisitos

| Componente | Versión | Notas |
|-----------|---------|-------|
| Windows SDK | 10.0.19041+ | Incluye D3D11, Media Foundation |
| Visual Studio | 2019+ o 2022 | MSVC compiler |
| CMake | 3.10+ | Build generator |
| Git | Cualquiera | Para clonar repositorio |

### Compilación paso a paso

#### 1. Clonar y navegar

```bash
git clone https://github.com/tu-usuario/ZeroLagMonitor.git
cd ZeroLagMonitor/windows_client
```

#### 2. Crear directorio build

```bash
mkdir build
cd build
```

#### 3. Generar proyecto Visual Studio con CMake

```bash
cmake .. -G "Visual Studio 16 2019" -A x64
```

O para VS 2022:
```bash
cmake .. -G "Visual Studio 17 2022" -A x64
```

#### 4. Compilar Release

```bash
cmake --build . --config Release
```

O si prefieres configurar en VS:
```bash
start ZeroLagMonitor.sln
```
Luego click en `Build > Build Solution` (Ctrl+Shift+B).

#### 5. Ejecutable

```
build/Release/MyrrorClient.exe
```

### CMakeLists.txt - Estructura Básica

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyrrorClient)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Librerías Windows SDK
find_package(d3d11 REQUIRED)
find_package(dxgi REQUIRED)

# Componentes del proyecto
add_executable(MyrrorClient
    src/main.cpp
    src/Client.cpp
    network/TCPReceiver.cpp
    decode/MediaFoundationDecoder.cpp
    render/DirectXRenderer.cpp
    ui/WebUI.cpp
    config/ConfigLoader.cpp
    util/Logger.cpp
)

target_link_libraries(MyrrorClient PUBLIC
    d3d11.lib
    dxgi.lib
    mf.lib
    mfuuid.lib
    ws2_32.lib
    ole32.lib
    oleaut32.lib
)

target_include_directories(MyrrorClient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    third_party
)
```

---

## Plan de Implementación

### 📅 Timeline: 4-7 días

### Sprint 1: Base + Receiver (1 día)

**Objetivos**:
- ✅ Proyecto CMake compilable
- ✅ Receiver TCP escuchando puerto 9000
- ✅ Main loop básico

**Archivos clave**:
- `src/main.cpp` - punto de entrada
- `network/TCPReceiver.hpp/cpp` - escuchar socket
- `config/ConfigLoader.hpp/cpp` - parsear args

**Verificación**:
```bash
windows_client.exe --server-ip 127.0.0.1
# Esperado: "Escuchando en puerto 9000..."
# Enviar paquetes MYRR desde Python → se reciben correctamente
```

---

### Sprint 2: Decodificador (1-2 días)

**Objetivos**:
- ✅ MediaFoundationDecoder funcionando
- ✅ Decodificar NALs a IMFSample
- ✅ Manejo de errores

**Archivos clave**:
- `decode/MediaFoundationDecoder.hpp/cpp`
- `decode/IDecoder.hpp`

**Verificación**:
```bash
# Logs deben mostrar:
# "Decodificador inicializado H.264 1920x1080"
# "Frame decodificado: PTS=123456, tamaño=8MB"
```

---

### Sprint 3: Rendering (1-2 días)

**Objetivos**:
- ✅ Ventana DirectX 11
- ✅ Shader NV12 → RGB
- ✅ Presentar frames a 60 FPS

**Archivos clave**:
- `render/DirectXRenderer.hpp/cpp`
- `render/Shaders/ps_nv12.hlsl`
- `ui/Window.hpp/cpp`

**Verificación**:
```bash
# Ejecutar con video host:
windows_client.exe --adb-forward
# Esperado: Video en pantalla fullscreen, 60 FPS
```

---

### Sprint 4: UI + Estadísticas (1 día)

**Objetivos**:
- ✅ WebUI panel HTTP (puerto 8081)
- ✅ JSON status con FPS, latencia
- ✅ Config GUI para conectar

**Archivos clave**:
- `ui/WebUI.hpp/cpp`
- `src/Client.cpp` - estadísticas

**Verificación**:
```bash
# Abrir navegador:
http://localhost:8081
# Esperado: Dashboard con status, botones Connect/Disconnect
```

---

### Sprint 5: Pulido + Testing (1-2 días)

**Objetivos**:
- ✅ Reconexión automática
- ✅ Sincronización de threads
- ✅ Testing 1+ horas sin crashes

**Verificación E2E**:
```bash
# Test 1: Latencia
# Esperar: < 10 ms latencia total

# Test 2: Estabilidad
# Dejar corriendo 30 minutos → 0 frames perdidos, CPU < 20%

# Test 3: Múltiples conexiones
# Conectar/desconectar 10 veces → mantiene estabilidad
```

---

## Verificación y Testing

### Test 1: Conexión Local USB-C (Baseline)

```bash
# Terminal 1: Host
cd windows_host/build/Release
ZeroLagMonitor.exe

# Terminal 2: Client
cd windows_client/build/Release
windows_client.exe --adb-forward
```

**Esperado**:
- ✅ Client muestra video en pantalla
- ✅ Sin lag visible
- ✅ FPS ≈ 60
- ✅ Latencia total < 10 ms

---

### Test 2: Conexión LAN Remota

```bash
# En PC A (Server - 192.168.1.50):
ZeroLagMonitor.exe

# En PC B (Client - 192.168.1.100):
windows_client.exe --server-ip 192.168.1.50 --port 9000
```

**Esperado**:
- ✅ Conecta después de 1-2 segundos
- ✅ Video fluido
- ✅ FPS = 60 (o cercano por congestión de red)

---

### Test 3: WebUI Dashboard

```bash
# Ejecutar client
windows_client.exe --server-ip 192.168.1.50

# Abrir navegador
http://localhost:8081

# Verificar:
# ✅ Status: "connected"
# ✅ FPS: 59-60
# ✅ Latency: { network: X, decode: Y, render: Z, total: T }
# ✅ Botón "Disconnect" funciona
```

---

### Test 4: Estabilidad 24 horas

```bash
# Script de testing
$start = Get-Date
$client = Start-Process "windows_client.exe" `
  -ArgumentList "--adb-forward" -PassThru

# Esperar 86400 segundos (24 horas)
Start-Sleep -Seconds 86400

# Verificar
$elapsed = (Get-Date) - $start
echo "Tiempo transcurrido: $elapsed"
echo "Frames recibidos: (check WebUI /api/status)"
echo "Frames perdidos: (check logs)"

Stop-Process -Id $client.Id
```

**Criterios de éxito**:
- ✅ 0 frames perdidos en 24 horas
- ✅ CPU utilization < 25%
- ✅ Memory leak < 100 MB
- ✅ Cero crashes o excepciones

---

### Test 5: Performance Benchmark

```cpp
// En DirectXRenderer::Present()

// Medir latencia frame-to-display
auto frame_arrival_time = current_frame.pts_us;
auto display_time = GetCurrentTimeUS();
auto latency_ms = (display_time - frame_arrival_time) / 1000.0;

LOG_INFO("Frame latency: {} ms (target < 16 ms @ 60 FPS)", latency_ms);

// Acumular estadísticas
latency_stats.push_back(latency_ms);
if (latency_stats.size() > 300) {  // 5 segundos a 60 FPS
    double avg = std::accumulate(latency_stats.begin(), latency_stats.end(), 0.0) 
                 / latency_stats.size();
    LOG_INFO("Avg latency: {} ms, Max: {}, Min: {}", 
             avg, *std::max_element(...), *std::min_element(...));
    latency_stats.clear();
}
```

---

## Próximas Fases (Futuro)

Una vez que el cliente Windows básico sea estable, se pueden agregar:

1. **IDD (Indirect Display Driver)**
   - Que el cliente se presente como monitor virtual nativo en Windows
   - Requiere driver certificado o testing mode

2. **Entrada Remota** (teclado/mouse)
   - Enviar eventos de input desde client → host
   - Permite control completo del PC remoto

3. **Multi-Monitor**
   - Soportar múltiples monitores en el host
   - Seleccionar cuál transmitir desde WebUI

4. **Compresión de Red**
   - Algoritmos de compresión para WiFi de baja velocidad
   - Mantener baja latencia

5. **Recording**
   - Grabar stream a archivo MP4
   - Para análisis posterior o compartir

---

## Contribuciones y Reportes de Bugs

Si encuentras problemas o tienes mejoras:

1. **Reportar bugs**: 
   - Abre un GitHub Issue con: versión Windows, GPU, pasos para reproducir
   - Adjunta logs (`windows_client.log`)

2. **PRs**: 
   - Crea feature branch: `git checkout -b feature/my-feature`
   - Sigue convención de commits: `[COMPONENT] Descripción`
   - Incluye tests y documentation

3. **Comunidad**:
   - Discusiones en GitHub Discussions
   - Documentación en Wiki del proyecto

---

## Licencia

Este proyecto es desarrollado bajo la misma licencia que ZeroLagMonitor. Ver `LICENSE.md`.

---

## Autor

**Emiliano Benito** - [GitHub](https://github.com/emi9310)

---

## Referencias Técnicas

- **Media Foundation**: https://docs.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk
- **Direct3D 11**: https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11
- **DXGI**: https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi
- **H.264/H.265**: ITU-T H.264 y H.265 specs (ISO/IEC standards)

---

**Última actualización**: Mayo 2026
