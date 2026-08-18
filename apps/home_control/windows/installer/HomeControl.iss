#define MyAppName "Home Control"
#define MyAppPublisher "AquaCYD"
#define MyAppExeName "HomeControl.exe"
#define MyAppId "{{4CF35972-CDA8-4A2A-B6E7-8E72D531FAF8}"

#ifndef AppVersion
  #error "AppVersion must be supplied by build_home_control_windows.ps1"
#endif

#ifndef AppBuildNumber
  #error "AppBuildNumber must be supplied by build_home_control_windows.ps1"
#endif

#ifndef SourceDir
  #error "SourceDir must be supplied by build_home_control_windows.ps1"
#endif

#ifndef OutputDir
  #error "OutputDir must be supplied by build_home_control_windows.ps1"
#endif

#ifndef OutputBaseFilename
  #error "OutputBaseFilename must be supplied by build_home_control_windows.ps1"
#endif

#ifndef VCRedistPath
  #error "VCRedistPath must be supplied by build_home_control_windows.ps1"
#endif

#ifndef VCRedistVersion
  #error "VCRedistVersion must be supplied by build_home_control_windows.ps1"
#endif

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#AppVersion}+{#AppBuildNumber}
AppVerName={#MyAppName} {#AppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/Baartek57548/AkwariumCYD
AppSupportURL=https://github.com/Baartek57548/AkwariumCYD/issues
AppUpdatesURL=https://github.com/Baartek57548/AkwariumCYD/releases
DefaultDirName={localappdata}\Programs\Home Control
DefaultGroupName=Home Control
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile=..\runner\resources\app_icon.ico
WizardImageFile=assets\wizard-large.bmp
WizardSmallImageFile=assets\wizard-small.bmp
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=110
DisableWelcomePage=no
DisableDirPage=no
DisableReadyPage=no
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (C) 2026 AquaCYD
VersionInfoDescription=Home Control Windows installer
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#AppVersion}.{#AppBuildNumber}
VersionInfoVersion={#AppVersion}.{#AppBuildNumber}

[Languages]
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#VCRedistPath}"; DestDir: "{tmp}"; DestName: "VC_redist.x64.exe"; Flags: deleteafterinstall; Check: VCRuntimeNeedsInstall; AfterInstall: InstallVCRuntime

[Icons]
Name: "{autoprograms}\Home Control"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; AppUserModelID: "AquaCYD.HomeControl"
Name: "{autodesktop}\Home Control"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon; AppUserModelID: "AquaCYD.HomeControl"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,Home Control}"; Flags: nowait postinstall skipifsilent

[CustomMessages]
polish.InstallingDependency=Instalowanie składnika: %1...
english.InstallingDependency=Installing component: %1...
polish.NewerVersionInstalled=Na tym koncie jest już zainstalowana nowsza wersja Home Control (%1). Instalacja starszej wersji została zablokowana.
english.NewerVersionInstalled=A newer Home Control version (%1) is already installed for this account. Downgrade has been blocked.
polish.VCRuntimeInstallFailed=Instalacja Microsoft Visual C++ Runtime zakończyła się kodem %1.
english.VCRuntimeInstallFailed=Microsoft Visual C++ Runtime installation exited with code %1.
polish.VCRuntimeVerificationFailed=Po instalacji nie wykryto wymaganej wersji Microsoft Visual C++ Runtime (%1).
english.VCRuntimeVerificationFailed=The required Microsoft Visual C++ Runtime version was not detected after installation (%1).

[Code]
const
  UninstallRegistryKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{4CF35972-CDA8-4A2A-B6E7-8E72D531FAF8}_is1';
  VCRuntimeRegistryKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

var
  VCRuntimeRestartRequired: Boolean;

function TakeVersionPart(var Version: String): Integer;
var
  Separator: Integer;
  Part: String;
begin
  Separator := Pos('.', Version);
  if Separator = 0 then
  begin
    Part := Version;
    Version := '';
  end
  else
  begin
    Part := Copy(Version, 1, Separator - 1);
    Delete(Version, 1, Separator);
  end;
  Result := StrToIntDef(Part, -1);
end;

function CompareSemanticVersions(LeftVersion, RightVersion: String): Integer;
var
  Index: Integer;
  LeftPart: Integer;
  RightPart: Integer;
begin
  Result := 0;
  StringChangeEx(LeftVersion, '+', '.', True);
  StringChangeEx(RightVersion, '+', '.', True);
  for Index := 1 to 4 do
  begin
    LeftPart := TakeVersionPart(LeftVersion);
    RightPart := TakeVersionPart(RightVersion);
    if LeftPart > RightPart then
    begin
      Result := 1;
      Exit;
    end;
    if LeftPart < RightPart then
    begin
      Result := -1;
      Exit;
    end;
  end;
end;

function VCRuntimeNeedsInstall: Boolean;
var
  Installed: Cardinal;
  InstalledVersion: String;
begin
  Result := True;
  if not RegQueryDWordValue(
       HKLM64,
       VCRuntimeRegistryKey,
       'Installed',
       Installed
     ) or
     (Installed <> 1) then
    Exit;

  if not RegQueryStringValue(
       HKLM64,
       VCRuntimeRegistryKey,
       'Version',
       InstalledVersion
     ) then
    Exit;

  if Length(InstalledVersion) > 0 then
  begin
    if (InstalledVersion[1] = 'v') or (InstalledVersion[1] = 'V') then
      Delete(InstalledVersion, 1, 1);
  end;

  Result := CompareSemanticVersions(
    InstalledVersion,
    ExpandConstant('{#VCRedistVersion}')
  ) < 0;
end;

procedure InstallVCRuntime;
var
  ErrorMessage: String;
  ResultCode: Integer;
  ResultCodeText: String;
  RequiredVersion: String;
begin
  RequiredVersion := ExpandConstant('{#VCRedistVersion}');
  if not Exec(
       ExpandConstant('{tmp}\VC_redist.x64.exe'),
       '/install /quiet /norestart',
       ExpandConstant('{tmp}'),
       SW_HIDE,
       ewWaitUntilTerminated,
       ResultCode
     ) then
  begin
    ResultCodeText := IntToStr(ResultCode);
    ErrorMessage := FmtMessage(CustomMessage('VCRuntimeInstallFailed'), [ResultCodeText]);
    RaiseException(ErrorMessage);
  end;

  if (ResultCode <> 0) and (ResultCode <> 3010) then
  begin
    ResultCodeText := IntToStr(ResultCode);
    ErrorMessage := FmtMessage(CustomMessage('VCRuntimeInstallFailed'), [ResultCodeText]);
    RaiseException(ErrorMessage);
  end;

  if VCRuntimeNeedsInstall then
  begin
    ErrorMessage := FmtMessage(CustomMessage('VCRuntimeVerificationFailed'), [RequiredVersion]);
    RaiseException(ErrorMessage);
  end;

  if ResultCode = 3010 then
    VCRuntimeRestartRequired := True;
end;

function NeedRestart(): Boolean;
begin
  Result := VCRuntimeRestartRequired;
end;

function InitializeSetup(): Boolean;
var
  InstalledVersion: String;
begin
  Result := True;
  if RegQueryStringValue(
       HKCU,
       UninstallRegistryKey,
       'DisplayVersion',
       InstalledVersion
     ) and
     (CompareSemanticVersions(
        InstalledVersion,
        ExpandConstant('{#AppVersion}+{#AppBuildNumber}')
      ) > 0) then
  begin
    if not WizardSilent then
      MsgBox(
        FmtMessage(CustomMessage('NewerVersionInstalled'), [InstalledVersion]),
        mbError,
        MB_OK
      );
    Result := False;
  end;
end;
