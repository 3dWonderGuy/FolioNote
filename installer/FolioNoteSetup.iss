#define MyAppName "FolioNote"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0-alpha"
#endif
#define MyAppPublisher "3dwonderguy"
#define MyAppURL "https://github.com/3dwonderguy/FolioNote"
#define MyAppExeName "FolioNote.exe"

[Setup]
AppId={{C8D49E22-5B90-4824-B831-75A0E63198AE}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
OutputDir=..\dist-installer
OutputBaseFilename=FolioNote-Setup-{#StringChange(MyAppVersion, '"', '')}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "printtofolionote"; Description: "Install 'Print to FolioNote' virtual printer (allows printing documents directly into FolioNote)"; GroupDescription: "System Integration:"; Flags: unchecked

[Files]
; Main Executable
Source: "..\dist\FolioNote\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Runtime DLLs (SDL3.dll, blend2d.dll, etc.)
Source: "..\dist\FolioNote\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Optional asset and config directories
Source: "..\dist\FolioNote\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Permissions: users-readexec
Source: "..\dist\FolioNote\config\*"; DestDir: "{app}\config"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\dist\FolioNote\LICENSE"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Main Executable
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

; Register "Print to FolioNote" virtual printer if task is selected
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""if (-not (Get-Printer -Name 'Print to FolioNote' -ErrorAction SilentlyContinue)) {{ Add-Printer -Name 'Print to FolioNote' -DriverName 'Microsoft Print To PDF' -PortName 'PORTPROMPT:' }}"""; StatusMsg: "Configuring 'Print to FolioNote' virtual printer..."; Tasks: printtofolionote; Flags: runhidden

[UninstallRun]
; Clean up "Print to FolioNote" printer on uninstallation
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""if (Get-Printer -Name 'Print to FolioNote' -ErrorAction SilentlyContinue) {{ Remove-Printer -Name 'Print to FolioNote' -ErrorAction SilentlyContinue }}"""; StatusMsg: "Removing 'Print to FolioNote' virtual printer..."; RunOnceId: "RemovePrintToFolioNote"; Flags: runhidden



