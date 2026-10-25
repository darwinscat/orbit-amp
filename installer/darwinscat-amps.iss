; Darwin's Cat Amps — the family's Windows installer (Inno Setup 6 or 7): every amp of one release in one
; Setup. The plug-ins are ONE bundle per format with every amp in it; the standalone apps are one per amp.
; The Components page lists the two plug-in formats, then "Standalone apps" with every amp's under it,
; all ticked; the player unticks what they do not want.
;
;   VST3        → Common Files\VST3\<bundle>.vst3
;   CLAP        → Common Files\CLAP\<bundle>.clap
;   Standalone  → Program Files\Darwin's Cat Amps\<Amp>.exe, with a Start-menu entry in Darwin's Cat\
;
; The amps, the plug-ins' name (<bundle>, "Darwin's Cat Amps") and the target that builds them come from
; installer\family.ini: taking an amp into the family is a line there, not here. Every amp keeps its
; plug-in ids, so a DAW session finds the same plug-in.
;
; THE PER-AMP PLUG-INS OF EARLIER RELEASES GO. Up to 0.8.0 every amp was a VST3 and a CLAP of its own
; (OrbitAmp.vst3, CatpitAmp.clap …, family.ini's old_bundles), with the ids the family's bundle carries
; now: a host that found both would list every amp twice. Installing a plug-in format deletes that
; format's old bundles ([InstallDelete]) — by their exact names, from the folder those installers
; placed them in, the same {autocf} this Setup uses, and from the per-user one of whoever runs Setup,
; {usercf} (a zip or an install for the current user alone puts them there, and a host reads both) —
; and nothing else. A format left unticked leaves its old bundles as they were.
;
; THE AMPS' DATA — the packs, the cabinets and the faces' pictures every amp plays, ONE copy of each —
; goes to ProgramData\Darwin's Cat\Factory, a component that is always installed and cannot be unticked
; (src\device\FactorySet.h): no amp carries any of it in its binary. It is the folder of the release's
; amps alone, as the build put it together, BuildDir\Shipped\Factory (CMakeLists.txt: THE FACTORY FOLDER).
;
; UNSIGNED for now: SmartScreen will warn ("Windows protected your PC" -> More info ->
; Run anyway) until code signing is added.
;
; Build (on Windows, after the CMake Release build of every amp):
;   ISCC.exe /DAppVersion=0.8.0 installer\darwinscat-amps.iss
; Overridable defines: AppVersion, BuildDir (the CMake build dir that holds <bundle_target>_artefacts and
; every <Amp>_artefacts).
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
#define Bundle       ReadIni(FamilyIni, "family", "bundle", "")
#define BundleTarget ReadIni(FamilyIni, "family", "bundle_target", "")
#define OldBundles   ReadIni(FamilyIni, "family", "old_bundles", "")
#define Plugs        BuildDir + "\" + BundleTarget + "_artefacts\Release"
#define AppPublisher "Darwin's Cat"
#define AppURL       "https://darwinscat.com"
#if AppName == "" || Amps == "" || Bundle == "" || BundleTarget == ""
  #error installer\family.ini names no family, amps, bundle or bundle_target
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
; Everything ticked, every time: what an earlier installer had ticked named other components than
; these.
UsePreviousSetupType=no
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=output
OutputBaseFilename=darwinscat-amps-{#AppVersion}-windows-x64-setup
; The strongest LZMA2 Inno has, in a process of its own, so the compressor has the memory it asks for.
Compression=lzma2/ultra64
LZMAUseSeparateProcess=yes
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; The one per-user place this Setup names on purpose: the old per-amp plug-ins it deletes from {usercf}.
UsedUserAreasWarning=no

[Types]
Name: "full";   Description: "Every plug-in format, every app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[InstallDelete]
; OrbitAmp's own installer kept its app and its uninstaller in {autopf}\OrbitAmp. The uninstaller
; goes whatever is ticked: the AppId is this Setup's now, and the old one would remove plug-ins that
; are this Setup's. The app goes when OrbitAmp's standalone is ticked, which places it again in the
; family's folder; unticked, it stays as it was.
Type: files;      Name: "{autopf}\OrbitAmp\unins*.*"
Type: files;      Name: "{autopf}\OrbitAmp\OrbitAmp.exe"; Components: standalone\orbitamp
Type: dirifempty; Name: "{autopf}\OrbitAmp"

[Components]
Name: "factory";    Description: "The amps' data: packs, cabinets, pictures (always installed)"; Types: full custom; Flags: fixed
Name: "vst3";       Description: "VST3 plug-in: every amp, in {#Bundle}.vst3"; Types: full
Name: "clap";       Description: "CLAP plug-in: every amp, in {#Bundle}.clap"; Types: full
Name: "standalone"; Description: "Standalone apps";                             Types: full

[Files]
Source: "{#BuildDir}\Shipped\Factory\*"; DestDir: "{commonappdata}\Darwin's Cat\Factory"; \
    Flags: recursesubdirs createallsubdirs ignoreversion; Components: factory
; VST3 is a bundle (a folder): its contents go into the shared VST3 folder.
Source: "{#Plugs}\VST3\{#Bundle}.vst3\*"; DestDir: "{autocf}\VST3\{#Bundle}.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
; CLAP on Windows is a single file (a DLL), into the shared CLAP folder.
Source: "{#Plugs}\CLAP\{#Bundle}.clap"; DestDir: "{autocf}\CLAP"; Flags: ignoreversion; Components: clap

[UninstallDelete]
; The bundle folder goes whole, with anything a host or the plug-in wrote into it.
Type: filesandordirs; Name: "{autocf}\VST3\{#Bundle}.vst3"; Components: vst3

; Every amp's part of the script, written once per amp of family.ini: its app and where it lands.
; Left holds the amps still to come, each followed by one space; the amp in hand is the first of them.
#define Left ""
#define AmpIn(str L)    Copy(L, 1, Pos(" ", L) - 1)
#define AfterAmp(str L) Copy(L, Pos(" ", L) + 1, Len(L))

#sub AmpEntries
  #define public Amp AmpIn(Left)
  #define public Low LowerCase(Amp)
  #define public Rel BuildDir + "\" + Amp + "_artefacts\Release"
[Components]
Name: "standalone\{#Low}"; Description: "{#Amp}"; Types: full

[Files]
Source: "{#Rel}\Standalone\{#Amp}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone\{#Low}

; The Start menu holds the family in a folder of its own, as /Applications does on the Mac (Darwin's Cat\
; <Amp>); the shortcut of the same name an earlier installer left in Programs itself goes.
[InstallDelete]
Type: files; Name: "{autoprograms}\{#Amp}.lnk"; Components: standalone\{#Low}

[Icons]
Name: "{autoprograms}\{#AppPublisher}\{#Amp}"; Filename: "{app}\{#Amp}.exe"; Components: standalone\{#Low}
#endsub

#for {Left = Amps + " "; Left != ""; Left = AfterAmp(Left)} AmpEntries

; An earlier release's per-amp plug-ins, written once per name of family.ini's old_bundles: each
; format's old bundle goes when that format is installed (see the top). A name is a plain word, or the
; script stops here — a wildcard or a path would reach further than one bundle.
#sub OldEntries
  #define public Old AmpIn(Left)
  #if Old != ""
    #if Pos("*", Old) || Pos("?", Old) || Pos("\", Old) || Pos("/", Old) || Pos(".", Old) || Pos(":", Old)
      #error installer\family.ini: old_bundles names something that is not a plain name
    #endif
[InstallDelete]
Type: filesandordirs; Name: "{autocf}\VST3\{#Old}.vst3"; Components: vst3
Type: files;          Name: "{autocf}\CLAP\{#Old}.clap"; Components: clap
Type: filesandordirs; Name: "{usercf}\VST3\{#Old}.vst3"; Components: vst3
Type: files;          Name: "{usercf}\CLAP\{#Old}.clap"; Components: clap
  #endif
#endsub

#if OldBundles != ""
  #for {Left = OldBundles + " "; Left != ""; Left = AfterAmp(Left)} OldEntries
#endif
