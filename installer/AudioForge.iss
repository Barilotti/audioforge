; AudioForge installer (Inno Setup 6).
; Built in CI from the Windows release build:
;   iscc /DAppVersion=0.1.0 /DSourceDir=<folder with AudioForge.exe and af-render.exe>
;        /DOutputDir=<output folder> installer\AudioForge.iss

#ifndef AppVersion
  #error AppVersion must be defined (/DAppVersion=x.y.z)
#endif
#ifndef SourceDir
  #error SourceDir must be defined (/DSourceDir=...)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; AppId identifies the product for upgrades and uninstall. Never change it.
AppId={{546D3EEF-CFF5-4839-B18A-D6636866716A}
AppName=AudioForge
AppVersion={#AppVersion}
AppVerName=AudioForge {#AppVersion}
AppPublisher=AudioForge
DefaultDirName={autopf}\AudioForge
DefaultGroupName=AudioForge
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=AudioForge-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
UninstallDisplayIcon={app}\AudioForge.exe
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\AudioForge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\af-render.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\AudioForge"; Filename: "{app}\AudioForge.exe"
Name: "{autodesktop}\AudioForge"; Filename: "{app}\AudioForge.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\AudioForge.exe"; Description: "{cm:LaunchProgram,AudioForge}"; Flags: nowait postinstall skipifsilent

; User data (%LOCALAPPDATA%\AudioForge: logs, crash dumps, caches) is kept on uninstall.
