; Darwin's Cat Amps — the family's Windows installer (Inno Setup 6 or 7): every amp of one release in one
; Setup. The Components page lists each amp with its formats under it, all ticked; the player
; unticks what he does not want.
;
;   VST3        → Common Files\VST3\<Amp>.vst3
;   CLAP        → Common Files\CLAP\<Amp>.clap
;   Standalone  → Program Files\Darwin's Cat Amps\<Amp>.exe, with a Start-menu entry
;
; The amps come from installer\family.ini: taking one into the family is a line there, not here.
; The installer only places files. Every amp carries its own data inside its binary and keeps its
; identity, so a DAW session finds the same plug-in in the same place.
;
; UNSIGNED for now: SmartScreen will warn ("Windows protected your PC" -> More info ->
; Run anyway) until code signing is added.
;
; Build (on Windows, after the CMake Release build of every amp):
;   ISCC.exe /DAppVersion=0.8.0 installer\darwinscat-amps.iss
; Overridable defines: AppVersion, BuildDir (the CMake build dir that holds <Amp>_artefacts).
;
; Setup runs as administrator, which the shared plug-in folders need. From the command line,
; /CURRENTUSER installs for the current user alone, without administrator rights: VST3 and CLAP then
; go to %LOCALAPPDATA%\Programs\Common, the per-user folder both formats name, and the apps to
; %LOCALAPPDATA%\Programs.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif

#define FamilyIni    AddBackslash(SourcePath) + "family.ini"
#define AppName      ReadIni(FamilyIni, "family", "name", "")
#define Amps         ReadIni(FamilyIni, "family", "amps", "")
#define AppPublisher "Darwin's Cat"
#define AppURL       "https://darwinscat.com"
#if AppName == "" || Amps == ""
  #error installer\family.ini names no family or no amps
#endif

[Setup]
; OrbitAmp's AppId (its own installer's, up to 0.7.0), kept on purpose: OrbitAmp's installer came first, so an
; OrbitAmp already installed is upgraded by this Setup in place — one entry under Installed apps, now
; the family's — rather than left beside it. Do NOT change it.
AppId={{62EA38AA-F6FB-446B-B92A-7F097A8ED667}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
; The family's folder, also over OrbitAmp's own install, whose folder [InstallDelete] clears.
UsePreviousAppDir=no
; Every amp and every format ticked, every time: what OrbitAmp's installer had ticked named other
; components than these.
UsePreviousSetupType=no
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=output
OutputBaseFilename=darwinscat-amps-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline

[Types]
Name: "full";   Description: "Every amp, every format"
Name: "custom"; Description: "Custom"; Flags: iscustom

[InstallDelete]
; OrbitAmp's own installer kept its app and its uninstaller in {autopf}\OrbitAmp. The uninstaller
; goes whatever is ticked: the AppId is this Setup's now, and the old one would remove plug-ins that
; are this Setup's. The app goes when OrbitAmp's standalone is ticked, which places it again in the
; family's folder; unticked, it stays as it was.
Type: files;      Name: "{autopf}\OrbitAmp\unins*.*"
Type: files;      Name: "{autopf}\OrbitAmp\OrbitAmp.exe"; Components: orbitamp\standalone
Type: dirifempty; Name: "{autopf}\OrbitAmp"

; Every amp's part of the script, written once per amp of family.ini: the amp, its three formats
; and where each lands. Left holds the amps still to come, each followed by one space; the amp in
; hand is the first of them.
#define Left ""
#define AmpIn(str L)    Copy(L, 1, Pos(" ", L) - 1)
#define AfterAmp(str L) Copy(L, Pos(" ", L) + 1, Len(L))

#sub AmpEntries
  #define public Amp AmpIn(Left)
  #define public Low LowerCase(Amp)
  #define public Rel BuildDir + "\" + Amp + "_artefacts\Release"
[Components]
Name: "{#Low}";            Description: "{#Amp}";         Types: full
Name: "{#Low}\vst3";       Description: "VST3 plug-in";   Types: full
Name: "{#Low}\clap";       Description: "CLAP plug-in";   Types: full
Name: "{#Low}\standalone"; Description: "Standalone app"; Types: full

[Files]
; VST3 is a bundle (a folder): its contents go into the shared VST3 folder.
Source: "{#Rel}\VST3\{#Amp}.vst3\*"; DestDir: "{autocf}\VST3\{#Amp}.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion; Components: {#Low}\vst3
; CLAP on Windows is a single file (a DLL), into the shared CLAP folder.
Source: "{#Rel}\CLAP\{#Amp}.clap"; DestDir: "{autocf}\CLAP"; Flags: ignoreversion; Components: {#Low}\clap
Source: "{#Rel}\Standalone\{#Amp}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: {#Low}\standalone

[Icons]
Name: "{autoprograms}\{#Amp}"; Filename: "{app}\{#Amp}.exe"; Components: {#Low}\standalone

[UninstallDelete]
; The bundle folder goes whole, with anything a host or the plug-in wrote into it.
Type: filesandordirs; Name: "{autocf}\VST3\{#Amp}.vst3"; Components: {#Low}\vst3
#endsub

#for {Left = Amps + " "; Left != ""; Left = AfterAmp(Left)} AmpEntries
