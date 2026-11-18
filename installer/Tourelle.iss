; Tourelle installer (Inno Setup 6).
;   iscc /DAppVersion=1.0.0 /DBinDir=..\native\build installer\Tourelle.iss
; BinDir holds Tourelle.exe, TourelleCamera.dll and tourelle-cli.exe.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BinDir
  #define BinDir "..\native\build"
#endif

[Setup]
AppId={{DF8DE8DE-FAFE-421F-BB25-749BAD74F43C}
AppName=Tourelle
AppVersion={#AppVersion}
AppVerName=Tourelle {#AppVersion}
AppPublisher=Tourelle contributors
AppPublisherURL=https://github.com/padashtan/tourelle
AppSupportURL=https://github.com/padashtan/tourelle/issues
AppUpdatesURL=https://github.com/padashtan/tourelle/releases
AppCopyright=Copyright (c) 2026 Tourelle contributors. MIT License.
DefaultDirName={autopf}\Tourelle
DisableDirPage=yes
; The camera source is registered for this exact folder (Program Files\Tourelle).
UsePreviousAppDir=no
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=tourelle-setup-{#AppVersion}
SetupIconFile=..\native\app\tourelle.ico
UninstallDisplayIcon={app}\Tourelle.exe
UninstallDisplayName=Tourelle
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 11 (virtual cameras need build 22000 or later).
MinVersion=10.0.22000
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"

[Files]
; TourelleCamera.dll is embedded in Tourelle.exe: "Tourelle.exe --install-quiet" writes it.
Source: "{#BinDir}\Tourelle.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\tourelle-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Tourelle"; Filename: "{app}\Tourelle.exe"

[Registry]
; Per-user values written by Tourelle.exe: removed on uninstall, never created here.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Tourelle"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Tourelle"; ValueType: none; Flags: uninsdeletekey

[Run]
Filename: "{app}\Tourelle.exe"; Description: "{cm:LaunchProgram,Tourelle}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
// The Windows Camera Frame Server keeps TourelleCamera.dll loaded. Stopping it
// (it restarts on demand) lets the files be replaced or removed without a reboot.
procedure StopFrameServer();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\sc.exe'), 'stop FrameServer', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\sc.exe'), 'stop FrameServerMonitor', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(2000);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopFrameServer();
  Result := '';
end;

// Installs the camera source (written from the copy embedded in Tourelle.exe) and
// switches the camera's driver when it is plugged in; otherwise Tourelle.exe offers
// that later. A failure is reported instead of looking like a success.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    if not Exec(ExpandConstant('{app}\Tourelle.exe'), '--install-quiet', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
       or (ResultCode <> 0) then
      SuppressibleMsgBox('Tourelle could not install its camera source (code ' + IntToStr(ResultCode) +
        '). Open Tourelle and click Install to retry.', mbError, MB_OK, IDOK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then StopFrameServer();
end;

[UninstallRun]
; Same account as the user who approved the UAC prompt: removes that user's virtual camera.
Filename: "{app}\tourelle-cli.exe"; Parameters: "remove"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveVirtualCamera"
Filename: "{app}\Tourelle.exe"; Parameters: "--uninstall-quiet"; Flags: runhidden waituntilterminated; RunOnceId: "RestoreDriver"
