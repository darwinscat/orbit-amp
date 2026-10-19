#!/usr/bin/env bash
# The family's zip, for a player who places the bundles himself: every amp of the release
# (installer/family.ini) in every format this system builds, ONE archive, darwinscat-amps-<version>-<system>.zip:
#
#   darwinscat-amps-<version>-<system>/
#     VST3/<Amp>.vst3   AU/<Amp>.component (macOS)   CLAP/<Amp>.clap   Standalone/<Amp>.app | <Amp>.exe
#     Factory/     the amps' data — packs, cabinets, pictures — which no amp carries in its binary
#     README.txt   where Factory goes: the place the installer would lay it, where every amp looks
#
# NO `._` FILES. The zips released up to 0.7.0 were made by a bare `ditto -c -k`, which keeps every extended
# attribute and resource fork as an AppleDouble `._<name>` entry beside the file — and macOS hangs
# attributes on what it makes — so wherever the archive was not opened by the Finder, a `._` file came out
# beside the real ones. Here the bundles are copied without them and archived without them.
#
# ONE ARCHITECTURE A MAC ZIP: <system> macos-arm64 or macos-x86_64 keeps that slice of every universal
# binary in the bundles (`lipo -thin`) and nothing of the other — half the download, for a player who
# places the bundles by hand and knows which Mac it is. Thinned AFTER signing and notarization: each slice of a
# universal binary carries its own signature, and the stapled ticket names both, so a thinned bundle
# verifies (`codesign --verify --deep --strict`) and passes Gatekeeper (`spctl`) as it was. Plain `macos`
# keeps them universal; the .pkg always does.
#
# Usage: build-family-zip.sh <version> <build-dir> <out-dir> <system>
#   e.g. installer/build-family-zip.sh 0.8.0 build dist macos-arm64
#        installer/build-family-zip.sh 0.8.0 build dist windows-arm64
# <build-dir> is the CMake build dir: each amp is read from <build-dir>/<Amp>_artefacts/Release, and an amp
# missing a format stops it; the data is <build-dir>/Shipped/Factory, the folder of the release's amps alone
# (CMakeLists.txt: THE FACTORY FOLDER). On macOS the bundles are expected signed and stapled already (as
# sign-family-macos.sh leaves them): the archive carries them as they are. On Linux the family ships a
# tarball with an installer instead (build-family-linux.sh).
set -euo pipefail

VERSION="${1:?usage: build-family-zip.sh <version> <build-dir> <out-dir> <system>}"
BUILD="${2:?missing build dir}"
OUT="${3:?missing output dir}"
SYSTEM="${4:?missing system, e.g. macos-arm64 or windows-x64}"
case "$SYSTEM" in
  macos-arm64)  THIN=arm64 ;;
  macos-x86_64) THIN=x86_64 ;;
  *)            THIN="" ;;
esac
INI="$(cd "$(dirname "$0")" && pwd)/family.ini"

AMPS="$(sed -n 's/^amps=//p' "$INI" | tr -d '\r')"
[ -n "$AMPS" ] || { echo "build-family-zip.sh: no amps in $INI" >&2; exit 1; }
NAME="darwinscat-amps-$VERSION-$SYSTEM"

case "$(uname -s)" in
  Darwin)               FORMATS="VST3/%s.vst3 AU/%s.component CLAP/%s.clap Standalone/%s.app" ;;
  MINGW*|MSYS*|CYGWIN*) FORMATS="VST3/%s.vst3 CLAP/%s.clap Standalone/%s.exe" ;;
  *) echo "build-family-zip.sh: macOS and Windows only — Linux has build-family-linux.sh" >&2; exit 1 ;;
esac

# Everything is found before anything is packed: a family release with a hole in it is not one.
for amp in $AMPS; do
  for f in $FORMATS; do
    # shellcheck disable=SC2059   # the format IS the pattern
    p="$BUILD/${amp}_artefacts/Release/$(printf "$f" "$amp")"
    [ -e "$p" ] || { echo "build-family-zip.sh: missing $p" >&2; exit 1; }
  done
done

FACTORY="$BUILD/Shipped/Factory"
for amp in $AMPS; do
  [ -s "$FACTORY/sets/$amp.txt" ] || { echo "build-family-zip.sh: no $FACTORY/sets/$amp.txt — build Family_All" >&2; exit 1; }
done
case "$(uname -s)" in
  Darwin) PLACE="/Library/Application Support/Darwin's Cat/Factory" ;;
  *)      PLACE="C:\\ProgramData\\Darwin's Cat\\Factory" ;;
esac

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
root="$work/$NAME"

for amp in $AMPS; do
  for f in $FORMATS; do
    # shellcheck disable=SC2059
    rel="$(printf "$f" "$amp")"
    mkdir -p "$root/$(dirname "$rel")"
    if command -v ditto > /dev/null; then
      ditto --norsrc --noextattr --noacl "$BUILD/${amp}_artefacts/Release/$rel" "$root/$rel"
    else
      cp -R "$BUILD/${amp}_artefacts/Release/$rel" "$root/$rel"
    fi
  done
done

rm -f "$OUT/$NAME.zip"
# One slice of every universal binary in the bundles, where the zip is one architecture's (see above).
if [ -n "$THIN" ]; then
  while IFS= read -r -d '' bin; do
    if lipo "$bin" -verify_arch arm64 x86_64 2> /dev/null; then
      lipo "$bin" -thin "$THIN" -output "$bin.thin"
      mv "$bin.thin" "$bin"
    fi
  done < <(find "$root" -type f -path '*/Contents/MacOS/*' -print0)
fi

# The data, as the installer would lay it, and the word where it goes.
if command -v ditto > /dev/null; then
  ditto --norsrc --noextattr --noacl "$FACTORY" "$root/Factory"
else
  cp -R "$FACTORY" "$root/Factory"
fi
cat > "$root/README.txt" <<README
Darwin's Cat Amps $VERSION

The amps play their packs, cabinets and pictures from ONE folder they share, Factory, which is here
beside them. Copy it, whole, to

    $PLACE

which is where every amp looks for it. Without it an amp says that its data folder is missing, and a
face opens on that notice alone. The installer of this release lays the folder there itself.

The plug-ins and the apps go wherever you keep yours.
README

if command -v ditto > /dev/null; then
  ditto -c -k --norsrc --noextattr --noacl --keepParent "$root" "$OUT/$NAME.zip"
elif command -v 7z > /dev/null; then
  ( cd "$work" && 7z a -tzip -bd "$OUT/$NAME.zip" "$NAME" > /dev/null )
else
  ( cd "$work" && zip -qr -X "$OUT/$NAME.zip" "$NAME" )
fi
echo "built $OUT/$NAME.zip"
