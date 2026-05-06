; ─────────────────────────────────────────────────────────────────────────────
; ZeroLag Monitor — Inno Setup Script
; ─────────────────────────────────────────────────────────────────────────────

#define AppName      "ZeroLag Monitor"
#define AppVersion   "1.0"
#define AppPublisher "ZeroLag"
#define AppExeName   "ZeroLagMonitor.exe"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\ZeroLag Monitor
DefaultGroupName={#AppName}
OutputDir=output
OutputBaseFilename=ZeroLagMonitor_Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#AppExeName}
DisableProgramGroupPage=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Files]
; ── Ejecutable principal ─────────────────────────────────────────────────────
Source: "..\windows_host\build\Release\ZeroLagMonitor.exe"; \
    DestDir: "{app}"; Flags: ignoreversion

; ── ADB ──────────────────────────────────────────────────────────────────────
Source: "redist\adb\adb.exe";          DestDir: "{app}\adb"; Flags: ignoreversion
Source: "redist\adb\AdbWinApi.dll";    DestDir: "{app}\adb"; Flags: ignoreversion
Source: "redist\adb\AdbWinUsbApi.dll"; DestDir: "{app}\adb"; Flags: ignoreversion

; ── Virtual Display Driver ───────────────────────────────────────────────────
Source: "redist\vdd\devcon.exe";       DestDir: "{app}\vdd"; Flags: ignoreversion
Source: "redist\vdd\MttVDD.dll";       DestDir: "{app}\vdd"; Flags: ignoreversion
Source: "redist\vdd\MttVDD.inf";       DestDir: "{app}\vdd"; Flags: ignoreversion
Source: "redist\vdd\mttvdd.cat";       DestDir: "{app}\vdd"; Flags: ignoreversion
Source: "redist\vdd\vdd_settings.xml"; DestDir: "{app}\vdd"; Flags: ignoreversion
; VDD Control — app de gestión de monitores virtuales
Source: "redist\vdd\VDD Control.exe";  DestDir: "{app}";     Flags: ignoreversion

[Icons]
Name: "{autodesktop}\{#AppName}";          Filename: "{app}\{#AppExeName}"
Name: "{group}\{#AppName}";                Filename: "{app}\{#AppExeName}"
Name: "{group}\VDD Control";               Filename: "{app}\VDD Control.exe"
Name: "{group}\Desinstalar {#AppName}";    Filename: "{uninstallexe}"

[Run]
; 1. Instalar el driver VDD con devcon (solo una vez, en la instalación)
Filename: "{app}\vdd\devcon.exe"; \
    Parameters: "install ""{app}\vdd\MttVDD.inf"" Root\MttVDD"; \
    StatusMsg: "Instalando Virtual Display Driver..."; \
    Flags: waituntilterminated runhidden

; 2. Iniciar la app al terminar (el usuario puede desmarcar)
Filename: "{app}\{#AppExeName}"; \
    Description: "Iniciar {#AppName} ahora"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; Remover el driver al desinstalar
Filename: "{app}\adb\adb.exe"; Parameters: "kill-server"; Flags: runhidden
