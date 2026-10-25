#!/usr/bin/env bash
# The family's zip, for a player who places the bundles himself: every amp of the release
# (installer/family.ini) in every format this system builds, ONE archive, darwinscat-amps-<version>-<system>.zip:
#
#   darwinscat-amps-<version>-<system>/
#     VST3/<bundle>.vst3   AU/<bundle>.component (macOS)   CLAP/<bundle>.clap
#                  the plug-ins: ONE bundle per format, every amp inside it (family.ini's `bundle=`)
#     Standalone/<Amp>.app | <Amp>.exe   the apps, one per amp
#     Factory/     the amps' data — packs, cabinets, pictures — which no amp carries in its binary
#     README.txt   where each of them goes — the folders the installer would use — and that the per-amp
#                  plug-ins of earlier releases (family.ini's `old_bundles=`), copied by hand, are to go
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
# <build-dir> is the CMake build dir: the plug-ins are read from <build-dir>/<bundle_target>_artefacts/Release,
# each amp's app from <build-dir>/<Amp>_artefacts/Release, and anything missing stops it; the data is <build-dir>/Shipped/Factory, the folder of the release's amps alone
# (CMakeLists.txt: THE FACTORY FOLDER). On macOS the bundles are expected signed and stapled already (as
# sign-family-macos.sh leaves them): the archive carries them as they are. On Linux the family ships a
# tarball with an installer instead (build-family-linux.sh).
set -euo pipefail
set -f   # no pathname expansion: family.ini's words are names, never patterns

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
ini() { sed -n "s/^$1=//p" "$INI" | tr -d '\r'; }

AMPS="$(ini amps)"
BUNDLE="$(ini bundle)"
TARGET="$(ini bundle_target)"
OLD="$(ini old_bundles)"
if [ -z "$AMPS" ] || [ -z "$BUNDLE" ] || [ -z "$TARGET" ]; then
  echo "build-family-zip.sh: no amps, bundle or bundle_target in $INI" >&2; exit 1
fi
NAME="darwinscat-amps-$VERSION-$SYSTEM"
PLUGS="$BUILD/${TARGET}_artefacts/Release"

case "$(uname -s)" in
  Darwin)               PLUGINS="VST3/%s.vst3 AU/%s.component CLAP/%s.clap"; APP="Standalone/%s.app" ;;
  MINGW*|MSYS*|CYGWIN*) PLUGINS="VST3/%s.vst3 CLAP/%s.clap";                 APP="Standalone/%s.exe" ;;
  *) echo "build-family-zip.sh: macOS and Windows only — Linux has build-family-linux.sh" >&2; exit 1 ;;
esac

# Everything is found before anything is packed: a family release with a hole in it is not one.
# shellcheck disable=SC2059   # the format IS the pattern
for f in $PLUGINS; do
  p="$PLUGS/$(printf "$f" "$BUNDLE")"
  [ -e "$p" ] || { echo "build-family-zip.sh: missing $p" >&2; exit 1; }
done
for amp in $AMPS; do
  # shellcheck disable=SC2059
  p="$BUILD/${amp}_artefacts/Release/$(printf "$APP" "$amp")"
  [ -e "$p" ] || { echo "build-family-zip.sh: missing $p" >&2; exit 1; }
done

FACTORY="$BUILD/Shipped/Factory"
for amp in $AMPS; do
  [ -s "$FACTORY/sets/$amp.txt" ] || { echo "build-family-zip.sh: no $FACTORY/sets/$amp.txt — build Family_All" >&2; exit 1; }
done
# Where the installer of this release puts each thing, which the README tells.
case "$(uname -s)" in
  Darwin)
    PLACE="/Library/Application Support/Darwin's Cat/Factory"
    WHERE="$(printf '    %-36s%s\n' \
      "VST3/$BUNDLE.vst3"      "/Library/Audio/Plug-Ins/VST3" \
      "AU/$BUNDLE.component"   "/Library/Audio/Plug-Ins/Components" \
      "CLAP/$BUNDLE.clap"      "/Library/Audio/Plug-Ins/CLAP")"
    MINE="Or into the same folders in your own ~/Library, for you alone."
    APPS="/Applications, or wherever you keep your apps"
    SUFFIXES=".vst3, .component and .clap" ;;
  *)
    PLACE="C:\\ProgramData\\Darwin's Cat\\Factory"
    WHERE="$(printf '    %-36s%s\n' \
      "VST3\\$BUNDLE.vst3"     "C:\\Program Files\\Common Files\\VST3" \
      "CLAP\\$BUNDLE.clap"     "C:\\Program Files\\Common Files\\CLAP")"
    MINE="The .vst3 is a folder: copy it whole."
    APPS="C:\\Program Files\\Darwin's Cat Amps, or wherever you keep your apps"
    SUFFIXES=".vst3 and .clap" ;;
esac
listing() {   # listing <words> — "A", "A and B", "A, B and C"
  local out="" n=$# i=0 w
  for w in "$@"; do
    i=$((i + 1))
    if [ $i -eq 1 ]; then out="$w"; elif [ $i -eq $n ]; then out+=" and $w"; else out+=", $w"; fi
  done
  printf '%s' "$out"
}
# shellcheck disable=SC2086   # the amps are words
EVERY="$(listing $AMPS)"
# shellcheck disable=SC2086
OLDS="$(listing $OLD)"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
root="$work/$NAME"

copy() {   # copy <from> <to> — a bundle or a file, without what ditto leaves out (see above)
  mkdir -p "$(dirname "$2")"
  if command -v ditto > /dev/null; then
    ditto --norsrc --noextattr --noacl "$1" "$2"
  else
    cp -R "$1" "$2"
  fi
}
# shellcheck disable=SC2059
for f in $PLUGINS; do
  rel="$(printf "$f" "$BUNDLE")"
  copy "$PLUGS/$rel" "$root/$rel"
done
for amp in $AMPS; do
  # shellcheck disable=SC2059
  rel="$(printf "$APP" "$amp")"
  copy "$BUILD/${amp}_artefacts/Release/$rel" "$root/$rel"
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

THE PLUG-INS are one bundle per format, and every amp of this release is in it: $EVERY.
Copy each into the folder your hosts look in:

$WHERE

$MINE

THE STANDALONE APPS, one per amp, are in Standalone: put them in $APPS.

THE AMPS' DATA: the amps play their packs, cabinets and pictures from ONE folder they share, Factory, which is here
beside them. Copy it, whole, to

    $PLACE

which is where every amp looks for it. Without it an amp says that its data folder is missing, and a
face opens on that notice alone. The installer of this release lays the folder there itself, and puts
everything else where this file says.
README
if [ -n "$OLD" ]; then
  cat >> "$root/README.txt" <<README

IF YOU COPIED THE PLUG-INS OF AN EARLIER RELEASE BY HAND — a bundle per amp: $OLDS, each as
$SUFFIXES — delete them, wherever you put them. $BUNDLE holds the same amps under the
same plug-in ids, and a host that finds both lists every amp twice. The installer removes the ones it
placed itself, and looks nowhere else.
README
fi

if command -v ditto > /dev/null; then
  ditto -c -k --norsrc --noextattr --noacl --keepParent "$root" "$OUT/$NAME.zip"
elif command -v 7z > /dev/null; then
  ( cd "$work" && 7z a -tzip -bd "$OUT/$NAME.zip" "$NAME" > /dev/null )
else
  ( cd "$work" && zip -qr -X "$OUT/$NAME.zip" "$NAME" )
fi
echo "built $OUT/$NAME.zip"
