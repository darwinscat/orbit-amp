# Installers

The amps are released together, as ONE release — «Darwin's Cat Amps», one version (`version.sh`), one
tag `vX.Y.Z` — with one installer per system, in which the player picks amps and formats; everything is
ticked to begin with. (Up to 0.7.0 OrbitAmp and CatpitAmp shipped apart, each with installers of its
own.)

The family's amps are listed once, in `family.ini` (`amps=OrbitAmp CatpitAmp RedPawAmp`), and all
family scripts read that line, and so does CMake's `Family_All`. Taking an amp into the family is adding
its name there.

**The plug-ins are ONE bundle per format for the whole family**, every amp inside it:
`Darwin's Cat Amps.vst3`, `Darwin's Cat Amps.component` (macOS) and `Darwin's Cat Amps.clap`. The
standalone apps stay one per amp (`<Amp>.app`, `<Amp>.exe`, `<Amp>`). `family.ini` names the bundle and the
target that builds it, and every script reads them from there:

| key             | what                                                                                 |
|-----------------|--------------------------------------------------------------------------------------|
| `name`          | the family, `Darwin's Cat Amps` — the installers' title                              |
| `amps`          | the amps of the release, in the installers' order; each builds `<Amp>_artefacts/Release/Standalone` |
| `bundle`        | the plug-ins' name, `Darwin's Cat Amps` — the stem of `<bundle>.vst3/.component/.clap` |
| `bundle_target` | the CMake target that builds them, `DarwinsCatAmps` → `<build>/DarwinsCatAmps_artefacts/Release/{VST3,AU,CLAP}` |
| `old_bundles`   | the per-amp plug-ins of earlier releases, `OrbitAmp CatpitAmp RedPawAmp`, which installing removes |

Each amp keeps its plug-in ids — plug-in code, manufacturer `Dcat` — inside the family's bundle, so a DAW
session finds the same plug-in.

**The per-amp plug-ins of earlier releases go.** Up to 0.8.0 every amp was a bundle of its own in every
format (`OrbitAmp.vst3`, `CatpitAmp.component`, `RedPawAmp.clap` …), carrying the same ids: a host that
found them beside the family's bundle would list every amp twice. Every installer, placing a plug-in
format, removes that format's old bundles — `old_bundles` by their exact names, from the one folder the
earlier installers placed them in, never a pattern and never a folder of the player's — and nothing
else. A format left unticked leaves its old bundles as they were. The zips' README tells a player who
copied the old bundles by hand to delete them.

**The amps' data is laid out once, by the installer.** The packs, the cabinets and the faces' pictures
every amp plays are in no binary: they lie in ONE folder the family shares, `Factory`, a part of every
installer that cannot be unticked — and each amp plays what its own list there names
(`Factory/sets/<Amp>.txt`; `src/device/FactorySet.h`). The installers lay out the folder of the
release's amps alone, as the build put it together: `<build>/Shipped/Factory` (`Family_All` makes it;
`CMakeLists.txt`, THE FACTORY FOLDER).

| system  | the folder                                                                  |
|---------|-----------------------------------------------------------------------------|
| macOS   | `/Library/Application Support/Darwin's Cat/Factory`                         |
| Windows | `C:\ProgramData\Darwin's Cat\Factory`                                       |
| Linux   | `~/.local/share/darwinscat-amps/Factory`, or `/usr/local/share/…` with `--system` |

An amp built from this tree reads `<build>/Factory` first, where it is there (`ORBITAMP_DEV_FACTORY`,
on by default); every release build passes `-DORBITAMP_DEV_FACTORY=OFF`.

## macOS — `build-family-pkg.sh`

    INSTALLER_IDENTITY="Developer ID Installer: …" installer/build-family-pkg.sh 0.8.0 build dist

- **In:** the version; the CMake build dir, from which the plug-ins,
  `<bundle_target>_artefacts/Release/{VST3,AU,CLAP}/<bundle>.*`, and every amp's
  `<Amp>_artefacts/Release/Standalone/<Amp>.app` are read — already codesigned and stapled, as
  `sign-family-macos.sh` leaves them; the out dir. A bundle without its binary stops it.
- **Out:** `dist/darwinscat-amps-<version>-macos.pkg`, a product archive with one component package per
  plug-in format, one per amp's app, and one for the data, `com.darwinscat.amps.factory`, into
  `/Library/Application Support/Darwin's Cat`. Customize shows the data first, ticked and greyed out,
  then VST3, Audio Unit and CLAP, then "Standalone apps" with every amp under it.
- **Compression:** every component `pkgbuild --compression latest --min-os-version 11.0` — the amps'
  own oldest macOS.
- **Signing:** only with `INSTALLER_IDENTITY` set (`productbuild --sign`); without it the package is
  unsigned, and `productsign` can sign it afterwards.

| part       | lands in                                                  | receipt                            |
|------------|-----------------------------------------------------------|------------------------------------|
| VST3       | `/Library/Audio/Plug-Ins/VST3/<bundle>.vst3`              | `com.darwinscat.amps.vst3`         |
| AU         | `/Library/Audio/Plug-Ins/Components/<bundle>.component`   | `com.darwinscat.amps.au`           |
| CLAP       | `/Library/Audio/Plug-Ins/CLAP/<bundle>.clap`              | `com.darwinscat.amps.clap`         |
| Standalone | `/Applications/<Amp>.app`                                 | `com.darwinscat.<amp>.standalone`  |

Each plug-in format's postinstall, once the family's bundle is in place, removes that format's
`old_bundles` from the same folder (`OrbitAmp.vst3` … in `/Library/Audio/Plug-Ins/VST3`) and forgets the
receipts that listed them: the 0.8.0 family's `com.darwinscat.<amp>.<format>` and the per-amp installers'
single `com.darwinscat.<amp>` of up to 0.7.0. Every app's postinstall forgets that single receipt too
(`pkgutil --forget` — the files stay; the new receipts list them). The folder is the one on the volume
Installer installs on (its `$3`), so a postinstall can be tried on a folder that stands for one:
`sh postinstall - - /tmp/fake-volume`. No bundle is relocatable: Installer places it where the table
says, never over a same-id copy it found somewhere else on the disk.

## Windows — `darwinscat-amps.iss`

    ISCC.exe /DAppVersion=0.8.0 installer\darwinscat-amps.iss

- **In:** `AppVersion`; `BuildDir`, the CMake build dir (`..\build` by default, from this folder),
  holding `<bundle_target>_artefacts\Release\{VST3\<bundle>.vst3\, CLAP\<bundle>.clap}` and every
  `<Amp>_artefacts\Release\Standalone\<Amp>.exe`.
- **Out:** `installer\output\darwinscat-amps-<version>-windows-x64-setup.exe`, unsigned.
- **Components:** `factory`, the data, into `{commonappdata}\Darwin's Cat\Factory` — `Flags: fixed`,
  always installed — then `vst3` and `clap`, the family's bundles, into `Common Files\VST3` and
  `Common Files\CLAP`, then `standalone` with `standalone\<amp>` under it, the apps, into
  `Program Files\Darwin's Cat Amps` with a Start-menu entry each. The uninstaller removes all of it.
- **The old per-amp plug-ins:** `[InstallDelete]` removes `{autocf}\VST3\<old>.vst3` with `vst3` and
  `{autocf}\CLAP\<old>.clap` with `clap`, for every name of `old_bundles` — the folder the earlier
  installers used, in either install mode. A name that is not a plain word stops the compile.
- **AppId** is OrbitAmp's own installer's, up to 0.7.0: an installed OrbitAmp is upgraded in place — one entry
  under Installed apps, now the family's — and its old folder and uninstaller are cleared.
- **Silent:** `/VERYSILENT` installs everything; `/COMPONENTS="vst3,standalone\orbitamp"` picks
  (`*standalone` takes every app). `/CURRENTUSER` installs without administrator rights,
  into the per-user folders (`%LOCALAPPDATA%\Programs\Common\VST3` and `\CLAP`).

## Linux — `build-family-linux.sh`

    installer/build-family-linux.sh 0.8.0 build dist x86_64

- **In:** the version; the CMake build dir (`<bundle_target>_artefacts/Release/{VST3,CLAP}` and every
  `<Amp>_artefacts/Release/Standalone`); the out dir; the architecture that names the tarball (`uname -m`
  by default). The amps' icons come from `resources/icon/<Amp>-1024.png`.
- **Out:** `dist/darwinscat-amps-<version>-linux-<arch>.tar.xz` — `VST3/<bundle>.vst3`,
  `CLAP/<bundle>.clap`, `Standalone/<Amp>`, `icons/`, `Factory/` (the data), `install.sh` /
  `uninstall.sh` (from `linux/`) and `release.ini` (the name, version, amps, `bundle` and `old_bundles`).

The player runs `./install.sh`: it asks for each plug-in format, then for each amp's app, or takes
`--yes`, `--formats vst3,clap`, `--amps orbitamp,redpawamp` (which apps), `--skip catpitamp:standalone`,
`--dry-run`. For
the player alone it places into `~/.vst3`, `~/.clap` and `~/.local/bin`, with a menu entry in
`~/.local/share/applications`; `--system` places for every user, through sudo, into
`/usr/local/lib/vst3`, `/usr/lib/clap` (the only system-wide folder CLAP names) and `/usr/local/bin`.
Whatever is picked, it places the data, `Factory`, in `~/.local/share/darwinscat-amps` (or
`/usr/local/share/darwinscat-amps`); an amp looks in the player's folder first, then in every user's.
What it placed is listed in `~/.local/share/darwinscat-amps/installed` (or `/usr/local/share/…`),
where it also leaves `uninstall.sh`, which removes exactly that. Placing a plug-in format removes that
format's `old_bundles` which an earlier `install.sh` placed — only paths its list names, in the folder of
the same mode — and takes them off the list; a bundle copied there by hand stays.

## The zip — `build-family-zip.sh` (macOS, Windows)

    installer/build-family-zip.sh 0.8.0 build dist macos-arm64
    installer/build-family-zip.sh 0.8.0 build dist windows-arm64

- **In:** the version; the CMake build dir; the out dir; the system that names the archive.
- **Out:** `dist/darwinscat-amps-<version>-<system>.zip`, one folder `darwinscat-amps-<version>-<system>/`
  with `VST3/<bundle>.vst3`, `AU/<bundle>.component` (macOS), `CLAP/<bundle>.clap`, `Standalone/` — every
  amp's app — and the data, `Factory/`, with a `README.txt` that says where each of them goes (the folders
  the installer would use) and tells a player who copied an earlier release's per-amp plug-ins by hand to
  delete them.
- **One architecture a Mac zip:** `macos-arm64` and `macos-x86_64` keep that slice of every universal
  binary (`lipo -thin`, after notarization — each slice keeps its own signature and the stapled ticket
  names both, so the thinned bundles verify and pass Gatekeeper as they were); `macos` keeps them universal.
  The release ships the two; the `.pkg` stays universal.
- **No `._` entries:** the bundles are copied and archived without extended attributes or resource forks
  (`ditto --norsrc --noextattr`), which a bare `ditto -c -k` stores as AppleDouble `._` files — the zips
  released up to 0.7.0 carried one beside nearly every file. On Windows it archives with `7z`.

## In the release workflow — `.github/workflows/release.yml`

One workflow, on a tag `vX.Y.Z` (or by hand, building without publishing unless told). Before anything is
built, `installer/version.sh "$TAG"` refuses a tag that is not `v` + the tree's version, and
`tools/changelog-collect.mjs --notes` refuses a version `CHANGELOG.md` has no section for. Then every system
builds `--target Family_All Family_Gates` and runs `tools/run-gates.sh build Release` — the one gate list,
`tests/gates.txt` — before it packs:

- **macOS** (the self-hosted signing runner, universal): `sign-family-macos.sh "$VER" build dist` — every
  bundle signed, one notarization for all of them, a ticket stapled on each; then `build-family-zip.sh …
  macos-arm64` and `… macos-x86_64`, and `INSTALLER_IDENTITY=… build-family-pkg.sh`, the package notarized
  and stapled.
- **Windows x64:** `ISCC.exe /DAppVersion=$VER installer\darwinscat-amps.iss` (Inno Setup 6 or 7, looked
  for, not assumed), and `build-family-zip.sh … windows-x64`. **Windows arm64:** the zip only.
- **Linux, x86_64 and arm64:** `build-family-linux.sh "$VER" build dist <arch>`.

The release `vX.Y.Z` is published as the repository's latest, with every file and a `SHA256SUMS`, and the
release's section of `CHANGELOG.md` as its notes.

## Locally — `release.sh`

`installer/release.sh` is the macOS job on this machine: a fresh `build-release/` with every sibling override
off, the same targets, the same gates, `sign-family-macos.sh`, and `SHA256SUMS` — into `dist/`. It needs the
Developer ID identities in the login keychain and a notarytool profile (`NOTARY_PROFILE`, default
`loki-notary`).
