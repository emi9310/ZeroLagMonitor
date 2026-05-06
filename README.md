# ZeroLagMonitor 🚀

ZeroLagMonitor es una solución de transmisión de pantalla de ultra baja latencia diseñada para visualizar contenido de Windows en dispositivos Android.

## 🛠 Estructura del Proyecto

El repositorio está dividido en tres componentes principales:

1.  **`windows_host/`**: El servidor escrito en C++ para Windows. Utiliza:
    *   **DXGI Desktop Duplication API** para la captura de pantalla de alto rendimiento.
    *   Codificación por hardware (**NVENC, AMF, QSV, MF**) para minimizar el impacto en la CPU.
    *   Servidor web integrado para comunicación.
2.  **`android_client/`**: Aplicación nativa de Android (Kotlin + C++ JNI).
    *   Decodificación de video mediante **MediaCodec** por hardware.
    *   Renderizado optimizado para reducir el jitter.
3.  **`installer/`**: Scripts de **Inno Setup** para generar el instalador de Windows, incluyendo dependencias como `adb` y controladores virtuales.

## 🚀 Requisitos y Configuración

### Windows Host
*   Visual Studio 2022 o CMake 3.10+.
*   Drivers de GPU actualizados para soporte de codificación por hardware.
*   **Compilación**: 
    ```bash
    cd windows_host
    mkdir build && cd build
    cmake ..
    cmake --build . --config Release
    ```

### Android Client
*   Android Studio Ladybug o superior.
*   NDK instalado para la parte de C++.
*   **Instalación**: Abre la carpeta `android_client` en Android Studio y dale a "Run".

## 🧪 Pruebas
Se incluye un script `test_sender.py` para realizar pruebas rápidas de conectividad y envío de paquetes sin necesidad de iniciar todo el pipeline de captura.

---
Desarrollado por [Emiliano Benito](https://github.com/emi9310)
