@echo off
:: ─────────────────────────────────────────────────────────────────────────────
:: prepare_redist.bat — Copia los archivos necesarios a installer\redist\
:: Ejecutar ANTES de compilar el instalador con Inno Setup
:: ─────────────────────────────────────────────────────────────────────────────

set ADB_SRC=D:\Archivos de Programas\AndroidSDK\platform-tools
set VDD_SRC=%LOCALAPPDATA%\Microsoft\WinGet\Packages\VirtualDrivers.Virtual-Display-Driver_Microsoft.Winget.Source_8wekyb3d8bbwe

echo [1/2] Copiando ADB...
mkdir redist\adb 2>nul
copy /Y "%ADB_SRC%\adb.exe"          redist\adb\
copy /Y "%ADB_SRC%\AdbWinApi.dll"    redist\adb\
copy /Y "%ADB_SRC%\AdbWinUsbApi.dll" redist\adb\

echo [2/2] Copiando Virtual Display Driver...
mkdir redist\vdd 2>nul
copy /Y "%VDD_SRC%\VDD Control.exe"                    redist\vdd\
copy /Y "%VDD_SRC%\Dependencies\devcon.exe"            redist\vdd\
copy /Y "%VDD_SRC%\Dependencies\vdd_settings.xml"      redist\vdd\
copy /Y "%VDD_SRC%\SignedDrivers\x86\VDD\MttVDD.dll"   redist\vdd\
copy /Y "%VDD_SRC%\SignedDrivers\x86\VDD\MttVDD.inf"   redist\vdd\
copy /Y "%VDD_SRC%\SignedDrivers\x86\VDD\mttvdd.cat"   redist\vdd\

echo.
echo Listo. Ahora abre ZeroLagMonitor.iss con Inno Setup Compiler.
pause
