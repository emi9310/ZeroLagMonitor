cmake_minimum_required(VERSION 3.20)
project(ZeroLagMonitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── GPU: AMD RX 570 → solo AMF ───────────────────────────────────
# AMF headers descargados en third_party/AMF/
# amfrt64.dll ya viene con los drivers AMD — no hay .lib que enlazar
set(AMF_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/AMF")

# ── Fuentes ──────────────────────────────────────────────────────
# NVENCEncoder y QSVEncoder se compilan pero Init() devuelve false
# en hardware AMD — sin coste en runtime, el factory elige AMF
add_executable(ZeroLagMonitor
    pipeline/Pipeline.cpp
    capture/DXGICapture.cpp
    encode/AMFEncoder.cpp       # GPU AMD RX 570
    encode/MFEncoder.cpp        # Windows Media Foundation — fallback universal
    encode/EncoderFactory.cpp
    network/AsyncSender.cpp
    gpu/GPUDetector.cpp
    ui/WebUI.cpp
    # NVENCEncoder.cpp y QSVEncoder.cpp excluidos — GPU AMD no los necesita
    # Sus clases son stubs en los .hpp para que EncoderFactory compile
)

# ── Includes ─────────────────────────────────────────────────────
target_include_directories(ZeroLagMonitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AMF_HEADERS}                             # AMF/core/*.h, AMF/components/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party    # httplib.h
)

# ── Librerías del sistema Windows ────────────────────────────────
target_link_libraries(ZeroLagMonitor PRIVATE
    d3d11 dxgi dxguid   # DXGI Desktop Duplication
    mf mfplat mfuuid    # Windows Media Foundation encoder
    ws2_32              # Winsock (UDP)
    shell32             # ShellExecute (abrir browser)
    # AMF  → runtime via LoadLibrary, sin .lib
    # NVENC → no presente, Init() falla silenciosamente
    # QSV   → no presente, Init() falla silenciosamente
)

# ── Defines globales Windows ─────────────────────────────────────
# WIN32_LEAN_AND_MEAN evita que windows.h incluya winsock.h antiguo
# que conflictúa con winsock2.h/ws2tcpip.h usado por httplib y el sender
target_compile_definitions(ZeroLagMonitor PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    MYRROR_GPU_AMD=1
    # Desactivar features opcionales de httplib que requieren libs externas
    CPPHTTPLIB_NO_EXCEPTIONS
)

# ── Sin ventana de consola ───────────────────────────────────────
# /SUBSYSTEM:WINDOWS → no abre consola al hacer doble click
# /ENTRY:mainCRTStartup → sigue usando main() como entry point normal
if(MSVC)
    set_target_properties(ZeroLagMonitor PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

# ── Flags de optimización ────────────────────────────────────────
if(MSVC)
    target_compile_options(ZeroLagMonitor PRIVATE /O2 /W3 /wd4996)
else()
    target_compile_options(ZeroLagMonitor PRIVATE -O3 -Wall)
endif()
