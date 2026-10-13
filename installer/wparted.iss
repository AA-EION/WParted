; WParted — Inno Setup Script
; Copyright (C) 2026 WParted Contributors
; Licensed under GNU GPLv2+

#define MyAppName "WParted"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "WParted Development Team"
#define MyAppURL "https://github.com/AA-EION/WParted"
#define MyAppExeName "wparted.exe"

[Setup]
AppId={{5E2D77C0-D9F4-4482-9BF9-E9BF576C789A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\COPYING
OutputDir=..\dist
OutputBaseFilename=WParted-v{#MyAppVersion}-win64-setup
SetupIconFile=..\resources\wparted.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "installwinfsp"; Description: "Install WinFsp (required for Read-Only APFS mounting)"; GroupDescription: "Storage Drivers:"; Flags: checked
Name: "wslprovision"; Description: "Provision WParted WSL2 Linux Filesystem Bridge"; GroupDescription: "Linux Filesystem Support:"; Flags: checked

[Files]
Source: "..\dist\WParted-portable\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\wparted.ico"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\wparted.ico"; Tasks: desktopicon

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\scripts\wparted-wsl-setup.ps1"""; StatusMsg: "Provisioning WSL2 Linux storage bridge..."; Tasks: wslprovision; Flags: runhidden
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\WParted"
