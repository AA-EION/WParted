; WParted - Inno Setup script
; Copyright (C) 2026 WParted contributors
; Licensed under the GNU General Public License version 2 or later
;
; Build after scripts/bundle-windows.sh:
;   iscc /DMyAppVersion=0.1.0 installer\wparted.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef BundleDir
  #define BundleDir "..\dist\WParted"
#endif

#define MyAppName "WParted"
#define MyAppPublisher "Issen Software Group"
#define MyAppURL "https://github.com/AA-EION/WParted"
#define MyAppExeName "wparted.exe"

[Setup]
AppId={{5E2D77C0-D9F4-4482-9BF9-E9BF576C789A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppCopyright=GParted authors and WParted contributors, GNU GPL v2 or later
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoDescription={#MyAppName} {#MyAppVersion} Setup
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
LicenseFile=..\COPYING
OutputDir=..\dist
OutputBaseFilename=WParted-{#MyAppVersion}-win64-setup
SetupIconFile=..\resources\wparted.ico
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; WParted itself always runs elevated, install it per machine where standard
; users cannot replace its files.
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Files of the 1.0.0-preview layout (executables in the top folder).
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\wparted*.exe"

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; WorkingDir: "{app}\bin"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
; wparted.exe requires administrator rights through its manifest: shellexec
; lets Windows show the elevation prompt.
Filename: "{app}\bin\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent shellexec
