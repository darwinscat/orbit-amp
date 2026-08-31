; OrbitAmp — NAM-based guitar rig (Felitronics by Darwin's Cat). Windows installer (Inno Setup 6).
;
; UNSIGNED for now: SmartScreen will warn ("Windows protected your PC" -> More info ->
; Run anyway) until code signing is added.
;
; Build (on Windows, after the CMake Release build):
;   ISCC.exe /DAppVersion=0.1.0 installer\orbitamp.iss
; Overridable defines: AppVersion, BuildDir (the CMake Release artefacts dir).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\OrbitAmp_artefacts\Release"
#endif

#define AppName      "OrbitAmp"
#define AppPublisher "Darwin's Cat"
#define AppURL       "https://darwinscat.com/orbitamp"

[Setup]
; Stable AppId (do NOT change post-release — it identifies the product for upgrades).
; This GUID is OrbitAmp's own — keep it stable.
AppId={{62EA38AA-F6FB-446B-B92A-7F097A8ED667}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=output
OutputBaseFilename=OrbitAmp-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Plugins live under Program Files\Common Files -> needs admin.
PrivilegesRequired=admin

[Components]
Name: "vst3";       Description: "VST3 plugin";    Types: full custom; Flags: fixed
Name: "clap";       Description: "CLAP plugin";    Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
; VST3 is a bundle (folder) — copy its contents into the shared VST3 location.
Source: "{#BuildDir}\VST3\OrbitAmp.vst3\*"; DestDir: "{commoncf64}\VST3\OrbitAmp.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
; CLAP on Windows is a single file (a DLL) — copy it into the shared CLAP location.
Source: "{#BuildDir}\CLAP\OrbitAmp.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
Source: "{#BuildDir}\Standalone\OrbitAmp.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\OrbitAmp.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\OrbitAmp.vst3"
Type: files;          Name: "{commoncf64}\CLAP\OrbitAmp.clap"
