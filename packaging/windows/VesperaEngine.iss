#ifndef SourceDir
  #error SourceDir must point at an assembled Vespera Windows distribution.
#endif
#ifndef Version
  #define Version "1.1.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBase
  #define OutputBase "VesperaEngine-1.1.0-Windows-x64-Setup"
#endif

[Setup]
AppId={{2F9DBA3D-04A3-4D1D-9C3F-4CB5F49AAB37}
AppName=Vespera Engine
AppVersion={#Version}
AppPublisher=Timingplanet
DefaultDirName={autopf}\Vespera Engine
DefaultGroupName=Vespera Engine
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBase}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#SourceDir}\branding\vespera_icon.ico
UninstallDisplayIcon={app}\VesperaHub.exe
PrivilegesRequired=admin
ChangesAssociations=yes

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Vespera Engine"; Filename: "{app}\VesperaHub.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Vespera Engine"; Filename: "{app}\VesperaHub.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Registry]
Root: HKA; Subkey: "Software\Classes\.vesperaproject"; ValueType: string; ValueName: ""; ValueData: "Vespera.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Vespera.Project"; ValueType: string; ValueName: ""; ValueData: "Vespera Engine Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Vespera.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\VesperaEditor.exe,0"
Root: HKA; Subkey: "Software\Classes\Vespera.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\VesperaEditor.exe"" ""%1"""

[Run]
Filename: "{app}\VesperaHub.exe"; Description: "Launch Vespera Engine"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
