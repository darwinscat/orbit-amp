#!/usr/bin/env bash
# The family's Linux release: ONE .tar.xz, darwinscat-amps-<version>-linux-<arch>, with every amp of
# the release (installer/family.ini) — in the family's VST3 and CLAP plug-ins, ONE bundle per format that
# holds every amp (family.ini's `bundle=`), and as a standalone app of its own — and the install.sh and
# uninstall.sh (installer/linux) that place them — for the player alone, or with --system for every
# user. Inside:
#
#   VST3/<bundle>.vst3   CLAP/<bundle>.clap   Standalone/<Amp>   icons/<Amp>.png
#   Factory/          the amps' data — packs, cabinets, pictures — which install.sh always places
#   install.sh   uninstall.sh   release.ini (the family's name, version, amps, bundle, and the per-amp
#                plug-ins of earlier releases, which install.sh takes away where an earlier one placed them)
#
# A player who would rather place things by hand finds each format in its own folder.
#
# Usage: build-family-linux.sh <version> <build-dir> <out-dir> [arch]
#   e.g. installer/build-family-linux.sh 0.8.0 build dist
# <build-dir> is the CMake build dir: the plug-ins are read from <build-dir>/<bundle_target>_artefacts/Release,
# each amp's app from <build-dir>/<Amp>_artefacts/Release, and anything missing stops the build; the data is <build-dir>/Shipped/Factory, the folder of the release's
# amps alone (CMakeLists.txt: THE FACTORY FOLDER). <arch> names the tarball (default: uname -m).
set -euo pipefail

VERSION="${1:?usage: build-family-linux.sh <version> <build-dir> <out-dir> [arch]}"
BUILD="${2:?missing build dir}"
OUT="${3:?missing output dir}"
ARCH="${4:-$(uname -m)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
INI="$HERE/family.ini"
ini() { sed -n "s/^$1=//p" "$INI" | tr -d '\r'; }

FAMILY="$(ini name)"
AMPS="$(ini amps)"
BUNDLE="$(ini bundle)"
TARGET="$(ini bundle_target)"
OLD="$(ini old_bundles)"
if [ -z "$FAMILY" ] || [ -z "$AMPS" ] || [ -z "$BUNDLE" ] || [ -z "$TARGET" ]; then
  echo "build-family-linux.sh: no name, amps, bundle or bundle_target in $INI" >&2; exit 1
fi
NAME="darwinscat-amps-$VERSION-linux-$ARCH"
PLUGS="$BUILD/${TARGET}_artefacts/Release"

# Everything is found before anything is packed. A VST3 is there when its bundle holds a library
# for some Linux architecture; the CLAP and the standalones are single files here.
ls "$PLUGS/VST3/$BUNDLE.vst3/Contents/"*-linux/"$BUNDLE.so" > /dev/null 2>&1 \
  || { echo "build-family-linux.sh: no library in $PLUGS/VST3/$BUNDLE.vst3" >&2; exit 1; }
[ -f "$PLUGS/CLAP/$BUNDLE.clap" ] && [ -s "$PLUGS/CLAP/$BUNDLE.clap" ] \
  || { echo "build-family-linux.sh: missing $PLUGS/CLAP/$BUNDLE.clap" >&2; exit 1; }
for amp in $AMPS; do
  f="$BUILD/${amp}_artefacts/Release/Standalone/$amp"
  [ -f "$f" ] && [ -s "$f" ] || { echo "build-family-linux.sh: missing $f" >&2; exit 1; }
done

FACTORY="$BUILD/Shipped/Factory"
for amp in $AMPS; do
  [ -s "$FACTORY/sets/$amp.txt" ] || { echo "build-family-linux.sh: no $FACTORY/sets/$amp.txt — build Family_All" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
root="$work/$NAME"
mkdir -p "$root/VST3" "$root/CLAP" "$root/Standalone" "$root/icons" "$OUT"

cp -R "$PLUGS/VST3/$BUNDLE.vst3" "$root/VST3/"
cp "$PLUGS/CLAP/$BUNDLE.clap" "$root/CLAP/"
chmod 755 "$root/CLAP/$BUNDLE.clap"
for amp in $AMPS; do
  cp "$BUILD/${amp}_artefacts/Release/Standalone/$amp" "$root/Standalone/"
  chmod 755 "$root/Standalone/$amp"
  # The menu entry's icon: the amp's own, from the tree (CMakeLists.txt's ICON_BIG on Linux).
  icon="$HERE/../resources/icon/$amp-1024.png"
  if [ -f "$icon" ]; then cp "$icon" "$root/icons/$amp.png"; fi
done

cp -R "$FACTORY" "$root/Factory"
cp "$HERE/linux/install.sh" "$HERE/linux/uninstall.sh" "$root/"
chmod 755 "$root/install.sh" "$root/uninstall.sh"
printf 'name=%s\nversion=%s\namps=%s\nbundle=%s\nold_bundles=%s\n' \
  "$FAMILY" "$VERSION" "$AMPS" "$BUNDLE" "$OLD" > "$root/release.ini"

# xz, not gzip: the same tar, and a third smaller. Every tar a player has reads it (`tar -xf`).
tar -C "$work" -cJf "$OUT/$NAME.tar.xz" "$NAME"
echo "built $OUT/$NAME.tar.xz"
