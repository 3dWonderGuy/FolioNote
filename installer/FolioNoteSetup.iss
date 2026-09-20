; ==============================================================================
; FolioNote Inno Setup Installation Script
; ==============================================================================
; 
; GENERAL WORKING PROCESS & SYSTEM INTEGRATION:
; ---------------------------------------------
; This script packages the FolioNote modern digital notebook application into a
; professional Windows installer (.exe). In addition to deploying the main
; application binary and its runtime dependencies (SDL3, Blend2D, PDFium), the
; installer is responsible for core system integrations:
;
; 1. Local Virtual Printer Integration ("Print to FolioNote"):
;    - Registers a system printer named "Print to FolioNote" using Windows' built-in
;      "Microsoft Print To PDF" driver (ntprint.inf) and the 'PORTPROMPT:' port.
;    - This enables any Windows software (browsers, Word, Adobe, CAD) to print
;      documents directly into FolioNote.
;    - Enabled by default in the installation tasks.
;
; 2. Shell & Context Menu Integration:
;    - Creates an Explorer context menu item under SystemFileAssociations\.pdf
;      allowing users to right-click any PDF and select "Import into FolioNote".
;    - Adds a Windows "Send to" shortcut in {usersendto}\FolioNote.
;
; 3. Dedicated Imports Directory:
;    - Pre-creates the user imports directory at {userdocs}\FolioNote\Imports
;      with appropriate read/write permissions for incoming print and import jobs.
;
; 4. Clean Uninstallation:
;    - Fully reverses all integrations, removes registry associations, shortcuts,
;      and deregisters the "Print to FolioNote" virtual printer from the Windows Spooler.
; ==============================================================================

#define MyAppName "FolioNote"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0-alpha"
#endif
#define MyAppPublisher "3dwonderguy"
#define MyAppURL "https://github.com/3dwonderguy/FolioNote"
#define MyAppExeName "FolioNote.exe"

[Setup]
; Unique application GUID for Windows Add/Remove Programs registry tracking
AppId={{C8D49E22-5B90-4824-B831-75A0E63198AE}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
UsePreviousAppDir=yes
ChangesAssociations=yes
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
; Desktop shortcut (optional, unchecked by default)
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; Context menu shell integration (checked by default)
Name: "contextmenu"; Description: "Add Explorer right-click integration ('Open in FolioNote' for library folders, 'Import into FolioNote' for PDFs)"; GroupDescription: "System Integration:"

; Virtual Printer installation (checked by default as requested for full application capabilities)
Name: "printtofolionote"; Description: "Install 'Print to FolioNote' virtual printer (allows printing documents directly into FolioNote)"; GroupDescription: "System Integration:"

[Dirs]
; Ensure the dedicated user Imports directory exists for virtual printer spools and file imports
Name: "{userdocs}\FolioNote\Imports"; Permissions: users-full

[Files]
; Main Executable
Source: "..\dist\FolioNote\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Runtime DLLs (SDL3.dll, blend2d.dll, pdfium.dll, etc.)
Source: "..\dist\FolioNote\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Application assets, configurations, and license agreement
Source: "..\dist\FolioNote\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Permissions: users-readexec
Source: "..\dist\FolioNote\config\*"; DestDir: "{app}\config"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\dist\FolioNote\LICENSE"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
; Start Menu and Desktop shortcuts
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

; Windows Explorer "Send to" integration: Allows Right-Click -> Send To -> FolioNote
Name: "{usersendto}\FolioNote"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--import"

[Registry]
; Windows Explorer Context Menu: "Import into FolioNote" for any PDF document
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\FolioNote"; ValueType: string; ValueName: ""; ValueData: "Import into FolioNote"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\FolioNote"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\FolioNote\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --import ""%1"""; Tasks: contextmenu

; Windows Explorer Directory Shell Context Menu: "Open in FolioNote" for .foliolib and .notebook folders
Root: HKA; Subkey: "Software\Classes\Directory\shell\FolioNote"; ValueType: string; ValueName: ""; ValueData: "Open in FolioNote"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\FolioNote"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\FolioNote"; ValueType: string; ValueName: "AppliesTo"; ValueData: "System.FileName:~< "".foliolib"" OR System.FileName:~< "".notebook"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\FolioNote\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: contextmenu

[Run]
; Option to launch FolioNote immediately following setup
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Clean up "FolioNotePrintDispatch" scheduled task on uninstallation
Filename: "schtasks.exe"; Parameters: "/delete /tn ""FolioNotePrintDispatch"" /f"; StatusMsg: "Removing 'Print to FolioNote' event dispatcher..."; RunOnceId: "RemoveFolioNotePrintTask"; Flags: runhidden
; Clean up "Print to FolioNote" virtual printer on uninstallation
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""if (Get-Printer -Name 'Print to FolioNote' -ErrorAction SilentlyContinue) {{ Remove-Printer -Name 'Print to FolioNote' -ErrorAction SilentlyContinue }}; Restart-Service -Name Spooler -Force -ErrorAction SilentlyContinue"""; StatusMsg: "Removing 'Print to FolioNote' virtual printer..."; RunOnceId: "RemovePrintToFolioNote"; Flags: runhidden

[Code]
// ==============================================================================
// VIRTUAL PRINTER & PRINT EVENT DISPATCH REGISTRATION PROCEDURE
// ==============================================================================
procedure ConfigurePrintToFolioNote();
var
  ResultCode: Integer;
  AppExe: String;
  XmlContent: String;
  TempXmlPath: String;
begin
  AppExe := ExpandConstant('{app}\{#MyAppExeName}');
  TempXmlPath := ExpandConstant('{tmp}\FolioNotePrintTask.xml');

  XmlContent :=
    '<?xml version="1.0" encoding="UTF-8"?>' + #13#10 +
    '<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">' + #13#10 +
    '  <RegistrationInfo><Description>Auto-dispatches printed PDFs into FolioNote</Description></RegistrationInfo>' + #13#10 +
    '  <Triggers>' + #13#10 +
    '    <EventTrigger>' + #13#10 +
    '      <Enabled>true</Enabled>' + #13#10 +
    '      <Subscription>&lt;QueryList&gt;&lt;Query Id="0" Path="Microsoft-Windows-PrintService/Operational"&gt;&lt;Select Path="Microsoft-Windows-PrintService/Operational"&gt;*[System[EventID=307]] and *[UserData[DocumentPrinted[Param5=''Print to FolioNote'']]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>' + #13#10 +
    '      <ValueQueries><Value name="PrintedFile">Event/UserData/DocumentPrinted/Param6</Value></ValueQueries>' + #13#10 +
    '    </EventTrigger>' + #13#10 +
    '  </Triggers>' + #13#10 +
    '  <Principals><Principal id="Author"><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>' + #13#10 +
    '  <Settings>' + #13#10 +
    '    <MultipleInstancesPolicy>Parallel</MultipleInstancesPolicy>' + #13#10 +
    '    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>' + #13#10 +
    '    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>' + #13#10 +
    '    <AllowHardTerminate>true</AllowHardTerminate>' + #13#10 +
    '    <StartWhenAvailable>true</StartWhenAvailable>' + #13#10 +
    '    <Enabled>true</Enabled>' + #13#10 +
    '    <Priority>7</Priority>' + #13#10 +
    '  </Settings>' + #13#10 +
    '  <Actions Context="Author">' + #13#10 +
    '    <Exec>' + #13#10 +
    '      <Command>' + AppExe + '</Command>' + #13#10 +
    '      <Arguments>--import "$(PrintedFile)"</Arguments>' + #13#10 +
    '    </Exec>' + #13#10 +
    '  </Actions>' + #13#10 +
    '</Task>';

  SaveStringToFile(TempXmlPath, XmlContent, False);

  // 1. Enable PrintService/Operational log and configure virtual printer
  Exec('powershell.exe',
       '-NoProfile -ExecutionPolicy Bypass -Command "wevtutil sl ''Microsoft-Windows-PrintService/Operational'' /e:true; if (-not (Get-Printer -Name ''Print to FolioNote'' -ErrorAction SilentlyContinue)) { Add-Printer -Name ''Print to FolioNote'' -DriverName ''Microsoft Print To PDF'' -PortName ''FILE:'' }; Set-Printer -Name ''Print to FolioNote'' -Comment ''Virtual PDF printer for importing documents into FolioNote''; Restart-Service -Name Spooler -Force -ErrorAction SilentlyContinue"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  // 2. Register event-triggered scheduled task
  Exec('schtasks.exe',
       '/create /tn "FolioNotePrintDispatch" /xml "' + TempXmlPath + '" /f',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  DeleteFile(TempXmlPath);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('printtofolionote') then
    begin
      ConfigurePrintToFolioNote();
    end;
  end;
end;

// ==============================================================================
// HELPER: CHECK IF FOLIONOTE EXECUTABLE IS RUNNING
// ==============================================================================
function IsProcessRunning(const FileName: String): Boolean;
var
  FSWbemLocator: Variant;
  FWMIService: Variant;
  FWbemObjectSet: Variant;
begin
  Result := False;
  try
    FSWbemLocator := CreateOleObject('WbemScripting.SWbemLocator');
    FWMIService := FSWbemLocator.ConnectServer('.', 'root\CIMV2');
    FWbemObjectSet := FWMIService.ExecQuery(Format('SELECT * FROM Win32_Process WHERE Name = "%s"', [FileName]));
    Result := (FWbemObjectSet.Count > 0);
  except
    Result := False;
  end;
end;

// ==============================================================================
// 1. NIGHTLY / EXPERIMENTAL BUILD WARNING PROMPT
// ==============================================================================
procedure InitializeWizard();
var
  VerStr: String;
begin
  VerStr := '{#MyAppVersion}';
  // If version contains nightly, alpha, beta, or dev, present experimental warning dialog
  if (Pos('nightly', LowerCase(VerStr)) > 0) or (Pos('alpha', LowerCase(VerStr)) > 0) or (Pos('beta', LowerCase(VerStr)) > 0) or (Pos('dev', LowerCase(VerStr)) > 0) then
  begin
    MsgBox('⚠️ WARNING: NIGHTLY / EXPERIMENTAL BUILD (' + VerStr + ')' + #13#10 + #13#10 +
           'This build is compiled automatically for testing and evaluation purposes.' + #13#10 +
           'Features may be incomplete, unstable, or contain experimental code changes.' + #13#10 + #13#10 +
           'Please back up your important notes and notebook documents before proceeding.', 
           mbInformation, MB_OK);
  end;
end;

// ==============================================================================
// 2. USER PROMPT FOR RUNNING APP & PREVIOUS VERSION UNINSTALLATION
// ==============================================================================
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
  UninstallString: String;
  AppGuid: String;
  UserChoice: Integer;
begin
  Result := True;

  // 1. Check if FolioNote.exe is currently running and ask user to save & close it
  while IsProcessRunning('{#MyAppExeName}') do
  begin
    UserChoice := MsgBox('FolioNote is currently running.' + #13#10 + #13#10 +
                         'Please save your work and close FolioNote before continuing installation.' + #13#10 + #13#10 +
                         'Click [Retry] after closing FolioNote, or [Cancel] to abort setup.',
                         mbConfirmation, MB_RETRYCANCEL);
    if UserChoice = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;

  // 2. Detect previous version registry entry
  AppGuid := '{C8D49E22-5B90-4824-B831-75A0E63198AE}';
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + AppGuid + '_is1', 'UninstallString', UninstallString) or
     RegQueryStringValue(HKCU64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + AppGuid + '_is1', 'UninstallString', UninstallString) or
     RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + AppGuid + '_is1', 'UninstallString', UninstallString) or
     RegQueryStringValue(HKCU32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + AppGuid + '_is1', 'UninstallString', UninstallString) then
  begin
    UninstallString := RemoveQuotes(UninstallString);
    if FileExists(UninstallString) then
    begin
      // Ask user for permission before uninstalling previous version
      UserChoice := MsgBox('A previous version of FolioNote was detected on this computer.' + #13#10 + #13#10 +
                           'Would you like to uninstall the previous version before installing this update?' + #13#10 + #13#10 +
                           '(Note: Your saved notebooks, notes, and libraries will remain safe and untouched).' + #13#10 + #13#10 +
                           'Click [Yes] to remove previous version, [No] to install over it, or [Cancel] to abort.',
                           mbConfirmation, MB_YESNOCANCEL);
      if UserChoice = IDYES then
      begin
        Exec(UninstallString, '/SILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      end
      else if UserChoice = IDCANCEL then
      begin
        Result := False;
        Exit;
      end;
    end;
  end;
end;
