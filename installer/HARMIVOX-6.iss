; HARMIVOX-6 Windows installer (Inno Setup 6)
; Built by GitHub Actions: iscc /DAppVersion=x.y.z /DBuildDir=<path> HARMIVOX-6.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\Harmivox6_artefacts\Release"
#endif

[Setup]
AppId={{4A024A1B-1924-4908-9CDB-4A98EDE31C6C}
AppName=HARMIVOX-6
AppVersion={#AppVersion}
AppVerName=HARMIVOX-6 {#AppVersion}
AppPublisher=RedFlame Ent
DefaultDirName={autopf}\RedFlame Ent\HARMIVOX-6
DefaultGroupName=HARMIVOX-6
DisableProgramGroupPage=yes
OutputDir=..\installer-output
OutputBaseFilename=HARMIVOX-6-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=HARMIVOX-6
UninstallDisplayIcon={app}\HARMIVOX-6.exe
CloseApplications=yes

[Types]
Name: "full"; Description: "Plugin and standalone app"
Name: "plugin"; Description: "Plugin only"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (FL Studio, Ableton and other hosts)"; Types: full plugin custom; Flags: fixed
Name: "standalone"; Description: "Standalone app (no DAW needed, handy for the Voice lab)"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: standalone; Flags: unchecked

[Files]
; The VST3 goes in the standard shared VST3 folder that FL and Ableton scan
Source: "{#BuildDir}\VST3\HARMIVOX-6.vst3\*"; DestDir: "{commoncf64}\VST3\HARMIVOX-6.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\HARMIVOX-6.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\INSTALL-WINDOWS.txt"; DestDir: "{app}"; DestName: "Read me.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\HARMIVOX-6"; Filename: "{app}\HARMIVOX-6.exe"; Components: standalone
Name: "{group}\Uninstall HARMIVOX-6"; Filename: "{uninstallexe}"
Name: "{autodesktop}\HARMIVOX-6"; Filename: "{app}\HARMIVOX-6.exe"; Tasks: desktopicon

[InstallDelete]
; clear an older copy of the plugin bundle so no stale files are left behind
Type: filesandordirs; Name: "{commoncf64}\VST3\HARMIVOX-6.vst3"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\HARMIVOX-6.vst3"

[Run]
Filename: "{app}\HARMIVOX-6.exe"; Description: "Open HARMIVOX-6 now"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[Messages]
FinishedLabel=HARMIVOX-6 is installed.%n%nIn FL Studio: Options > Manage plugins > Find plugins. It appears under Effects as HARMIVOX-6 (RedFlame Ent).
