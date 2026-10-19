# Installers

The amps are released together, as ONE release — «Darwin's Cat Amps», one version (`version.sh`), one
tag `vX.Y.Z` — with one installer per system, in which the player picks amps and formats; everything is
ticked to begin with. (Up to 0.7.0 OrbitAmp and CatpitAmp shipped apart, each with installers of its
own.)

The family's amps are listed once, in `family.ini` (`amps=OrbitAmp CatpitAmp RedPawAmp`), and all
family scripts read that line, and so does CMake's `Family_All`. Taking an amp into the family is adding
its name there.

Each amp keeps its identity — bundle id `com.darwinscat.<amp>`, plug-in code, manufacturer `Dcat` — so
a DAW session finds the same plug-in in the same place.

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

- **In:** the version; the CMake build dir, from which every amp's
  `<Amp>_artefacts/Release/{VST3,AU,CLAP,Standalone}` is read — already codesigned and stapled, as
  `sign-family-macos.sh` leaves them; the out dir. A bundle without its binary stops it.
- **Out:** `dist/darwinscat-amps-<version>-macos.pkg`, a product archive with one component package per
  amp × format, and one for the data, `com.darwinscat.amps.factory`, into
  `/Library/Application Support/Darwin's Cat`. Customize shows the data first, ticked and greyed out,
  then each amp with VST3, Audio Unit, CLAP and Standalone under it.
- **Compression:** every component `pkgbuild --compression latest --min-os-version 11.0` — the amps'
  own oldest macOS.
- **Signing:** only with `INSTALLER_IDENTITY` set (`productbuild --sign`); without it the package is
  unsigned, and `productsign` can sign it afterwards.

| format     | lands in                              | receipt                            |
|------------|---------------------------------------|------------------------------------|
| VST3       | `/Library/Audio/Plug-Ins/VST3`        | `com.darwinscat.<amp>.vst3`        |
| AU         | `/Library/Audio/Plug-Ins/Components`  | `com.darwinscat.<amp>.au`          |
| CLAP       | `/Library/Audio/Plug-Ins/CLAP`        | `com.darwinscat.<amp>.clap`        |
| Standalone | `/Applications`                       | `com.darwinscat.<amp>.standalone`  |

The per-amp installers of up to 0.7.0 kept a whole amp under ONE receipt, `com.darwinscat.<amp>`. One receipt cannot be
four choices, so each format has its own now, and every component's postinstall forgets the amp's
old receipt (`pkgutil --forget` — the files stay; the new receipts list them). No bundle is
relocatable: Installer places it where the table says, never over a same-id copy it found
somewhere else on the disk.

## Windows — `darwinscat-amps.iss`

    ISCC.exe /DAppVersion=0.8.0 installer\darwinscat-amps.iss

- **In:** `AppVersion`; `BuildDir`, the CMake build dir (`..\build` by default, from this folder),
  holding `<Amp>_artefacts\Release\{VST3\<Amp>.vst3\, CLAP\<Amp>.clap, Standalone\<Amp>.exe}`.
- **Out:** `installer\output\darwinscat-amps-<version>-windows-x64-setup.exe`, unsigned.
- **Components:** `factory`, the data, into `{commonappdata}\Darwin's Cat\Factory` — `Flags: fixed`,
  always installed — then `<amp>`, with `<amp>\vst3`, `<amp>\clap`, `<amp>\standalone` under it. VST3 and
  CLAP go to `Common Files\VST3` and `Common Files\CLAP`, the apps to `Program Files\Darwin's Cat Amps`
  with a Start-menu entry each. The uninstaller removes all of it.
- **AppId** is OrbitAmp's own installer's, up to 0.7.0: an installed OrbitAmp is upgraded in place — one entry
  under Installed apps, now the family's — and its old folder and uninstaller are cleared.
- **Silent:** `/VERYSILENT` installs everything; `/COMPONENTS="*orbitamp,catpitamp\vst3"` picks
  (a `*` takes an amp with all its formats). `/CURRENTUSER` installs without administrator rights,
  into the per-user folders (`%LOCALAPPDATA%\Programs\Common\VST3` and `\CLAP`).

## Linux — `build-family-linux.sh`

    installer/build-family-linux.sh 0.8.0 build dist x86_64

- **In:** the version; the CMake build dir (`<Amp>_artefacts/Release/{VST3,CLAP,Standalone}`); the out
  dir; the architecture that names the tarball (`uname -m` by default). The amps' icons come from
  `resources/icon/<Amp>-1024.png`.
- **Out:** `dist/darwinscat-amps-<version>-linux-<arch>.tar.gz` — `VST3/`, `CLAP/`, `Standalone/`,
  `icons/`, `Factory/` (the data), and `install.sh` / `uninstall.sh` (from `linux/`).

The player runs `./install.sh`: it asks amp by amp and format by format, or takes `--yes`,
`--amps orbitamp,redpawamp`, `--formats vst3,clap`, `--skip catpitamp:standalone`, `--dry-run`. For
the player alone it places into `~/.vst3`, `~/.clap` and `~/.local/bin`, with a menu entry in
`~/.local/share/applications`; `--system` places for every user, through sudo, into
`/usr/local/lib/vst3`, `/usr/lib/clap` (the only system-wide folder CLAP names) and `/usr/local/bin`.
Whatever is picked, it places the data, `Factory`, in `~/.local/share/darwinscat-amps` (or
`/usr/local/share/darwinscat-amps`); an amp looks in the player's folder first, then in every user's.
What it placed is listed in `~/.local/share/darwinscat-amps/installed` (or `/usr/local/share/…`),
where it also leaves `uninstall.sh`, which removes exactly that.

## The zip — `build-family-zip.sh` (macOS, Windows)

    installer/build-family-zip.sh 0.8.0 build dist macos
    installer/build-family-zip.sh 0.8.0 build dist windows-arm64

- **In:** the version; the CMake build dir; the out dir; the system that names the archive.
- **Out:** `dist/darwinscat-amps-<version>-<system>.zip`, one folder `darwinscat-amps-<version>-<system>/`
  with `VST3/`, `AU/` (macOS), `CLAP/` and `Standalone/` — every amp of `family.ini` in each — and the
  data, `Factory/`, with a `README.txt` that says where to copy it (the folder the installer would lay).
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
  macos` and `INSTALLER_IDENTITY=… build-family-pkg.sh`, the package notarized and stapled.
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
