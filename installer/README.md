# Installers

Two kinds live here. The **per-amp** ones (`build-pkg.sh`, `orbitamp.iss`, `release.sh`) make one
amp's release, as OrbitAmp and CatpitAmp have shipped so far. The **family** ones make ONE release of
several amps — «Darwin's Cat Amps» — with one installer per system, in which the player picks amps
and formats; everything is ticked to begin with.

The family's amps are listed once, in `family.ini` (`amps=OrbitAmp CatpitAmp RedPawAmp`), and all
three family installers read that line. Taking an amp into the family is adding its name there.

Every installer only places bundles. Each amp carries its own data inside its binary, and keeps its
identity — bundle id `com.darwinscat.<amp>`, plug-in code, manufacturer `Dcat` — so a DAW session
finds the same plug-in in the same place.

## macOS — `build-family-pkg.sh`

    INSTALLER_IDENTITY="Developer ID Installer: …" installer/build-family-pkg.sh 0.8.0 build dist

- **In:** the version; the CMake build dir, from which every amp's
  `<Amp>_artefacts/Release/{VST3,AU,CLAP,Standalone}` is read — already codesigned and stapled, as
  `release.sh` and the release workflows leave them; the out dir. A bundle without its binary stops it.
- **Out:** `dist/darwinscat-amps-<version>-macos.pkg`, a product archive with one component package per
  amp × format. Customize shows each amp with VST3, Audio Unit, CLAP and Standalone under it.
- **Signing:** only with `INSTALLER_IDENTITY` set (`productbuild --sign`); without it the package is
  unsigned, and `productsign` can sign it afterwards.

| format     | lands in                              | receipt                            |
|------------|---------------------------------------|------------------------------------|
| VST3       | `/Library/Audio/Plug-Ins/VST3`        | `com.darwinscat.<amp>.vst3`        |
| AU         | `/Library/Audio/Plug-Ins/Components`  | `com.darwinscat.<amp>.au`          |
| CLAP       | `/Library/Audio/Plug-Ins/CLAP`        | `com.darwinscat.<amp>.clap`        |
| Standalone | `/Applications`                       | `com.darwinscat.<amp>.standalone`  |

`build-pkg.sh` kept a whole amp under ONE receipt, `com.darwinscat.<amp>`. One receipt cannot be
four choices, so each format has its own now, and every component's postinstall forgets the amp's
old receipt (`pkgutil --forget` — the files stay; the new receipts list them). No bundle is
relocatable: Installer places it where the table says, never over a same-id copy it found
somewhere else on the disk.

## Windows — `darwinscat-amps.iss`

    ISCC.exe /DAppVersion=0.8.0 installer\darwinscat-amps.iss

- **In:** `AppVersion`; `BuildDir`, the CMake build dir (`..\build` by default, from this folder),
  holding `<Amp>_artefacts\Release\{VST3\<Amp>.vst3\, CLAP\<Amp>.clap, Standalone\<Amp>.exe}`.
- **Out:** `installer\output\darwinscat-amps-<version>-windows-x64-setup.exe`, unsigned.
- **Components:** `<amp>`, with `<amp>\vst3`, `<amp>\clap`, `<amp>\standalone` under it. VST3 and
  CLAP go to `Common Files\VST3` and `Common Files\CLAP`, the apps to `Program Files\Darwin's Cat Amps`
  with a Start-menu entry each. The uninstaller removes all of it.
- **AppId** is OrbitAmp's (`orbitamp.iss`): an installed OrbitAmp is upgraded in place — one entry
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
  `icons/`, and `install.sh` / `uninstall.sh` (from `linux/`).

The player runs `./install.sh`: it asks amp by amp and format by format, or takes `--yes`,
`--amps orbitamp,redpawamp`, `--formats vst3,clap`, `--skip catpitamp:standalone`, `--dry-run`. For
the player alone it places into `~/.vst3`, `~/.clap` and `~/.local/bin`, with a menu entry in
`~/.local/share/applications`; `--system` places for every user, through sudo, into
`/usr/local/lib/vst3`, `/usr/lib/clap` (the only system-wide folder CLAP names) and `/usr/local/bin`.
What it placed is listed in `~/.local/share/darwinscat-amps/installed` (or `/usr/local/share/…`),
where it also leaves `uninstall.sh`, which removes exactly that.

## In the release workflow

What the family release will call, once it is wired in:

- **macOS:** build `OrbitAmp_All OrbitAmp_CLAP CatpitAmp_All RedPawAmp_All` (OrbitAmp's CLAP is not part
  of `OrbitAmp_All` and has to be named), sign, notarize and staple the bundles as the per-amp jobs
  do, then `INSTALLER_IDENTITY="$INST_ID" installer/build-family-pkg.sh "$VER" build dist`, and
  notarize and staple the package.
- **Windows x64:** the same targets, then
  `ISCC.exe /DAppVersion=$VER installer\darwinscat-amps.iss`.
- **Linux, x86_64 and arm64:** the same targets, then
  `installer/build-family-linux.sh "$VER" build dist <arch>`.
