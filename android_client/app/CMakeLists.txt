cmake_minimum_required(VERSION 3.22.1)
project("myrror_android")

# ── Librería nativa principal ────────────────────────────────────
add_library(
    myrror_android
    SHARED
    src/main/cpp/JNIBridge.cpp
    src/main/cpp/Receiver.cpp
    # MediaCodecDecoder.cpp es incluido desde JNIBridge.cpp directamente
)

# ── Flags de baja latencia ───────────────────────────────────────
target_compile_options(myrror_android PRIVATE
    -O3                # máxima optimización
    -ffast-math
    -std=c++17
)

# ── Librerías del sistema Android (NDK) ─────────────────────────
#   mediandk   → AMediaCodec, AMediaFormat
#   android    → ANativeWindow
#   log        → __android_log_print
target_link_libraries(myrror_android
    mediandk
    android
    log
)
