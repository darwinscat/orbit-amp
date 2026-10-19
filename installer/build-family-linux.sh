#!/usr/bin/env bash
# The family's Linux release: ONE .tar.gz, darwinscat-amps-<version>-linux-<arch>, with every amp of
# the release (installer/family.ini) as VST3, CLAP and the standalone app, and the install.sh and
# uninstall.sh (installer/linux) that place them — for the player alone, or with --system for every
# user. Inside:
#
#   VST3/<Amp>.vst3   CLAP/<Amp>.clap   Standalone/<Amp>   icons/<Amp>.png
#   Factory/          the amps' data — packs, cabinets, pictures — which install.sh always places
#   install.sh   uninstall.sh   release.ini (the family's name, version and amps)
#
# A player who would rather place things by hand finds each format in its own folder.
#
# Usage: build-family-linux.sh <version> <build-dir> <out-dir> [arch]
#   e.g. installer/build-family-linux.sh 0.8.0 build dist
# <build-dir> is the CMake build dir: each amp is read from <build-dir>/<Amp>_artefacts/Release, and an
# amp missing a format stops the build; the data is <build-dir>/Shipped/Factory, the folder of the release's
# amps alone (CMakeLists.txt: THE FACTORY FOLDER). <arch> names the tarball (default: uname -m).
set -euo pipefail

VERSION="${1:?usage: build-family-linux.sh <version> <build-dir> <out-dir> [arch]}"
BUILD="${2:?missing build dir}"
OUT="${3:?missing output dir}"
ARCH="${4:-$(uname -m)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
INI="$HERE/family.ini"

FAMILY="$(sed -n 's/^name=//p' "$INI")"
AMPS="$(sed -n 's/^amps=//p' "$INI")"
if [ -z "$FAMILY" ] || [ -z "$AMPS" ]; then
  echo "build-family-linux.sh: no name or no amps in $INI" >&2; exit 1
fi
NAME="darwinscat-amps-$VERSION-linux-$ARCH"

# Everything is found before anything is packed. A VST3 is there when its bundle holds a library
# for some Linux architecture; the CLAP and the standalone are single files here.
for amp in $AMPS; do
  rel="$BUILD/${amp}_artefacts/Release"
  ls "$rel/VST3/$amp.vst3/Contents/"*-linux/"$amp.so" > /dev/null 2>&1 \
    || { echo "build-family-linux.sh: no library in $rel/VST3/$amp.vst3" >&2; exit 1; }
  for f in "CLAP/$amp.clap" "Standalone/$amp"; do
    [ -f "$rel/$f" ] && [ -s "$rel/$f" ] || { echo "build-family-linux.sh: missing $rel/$f" >&2; exit 1; }
  done
done

FACTORY="$BUILD/Shipped/Factory"
for amp in $AMPS; do
  [ -s "$FACTORY/sets/$amp.txt" ] || { echo "build-family-linux.sh: no $FACTORY/sets/$amp.txt — build Family_All" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
root="$work/$NAME"
mkdir -p "$root/VST3" "$root/CLAP" "$root/Standalone" "$root/icons" "$OUT"

for amp in $AMPS; do
  rel="$BUILD/${amp}_artefacts/Release"
  cp -R "$rel/VST3/$amp.vst3" "$root/VST3/"
  cp "$rel/CLAP/$amp.clap" "$root/CLAP/"
  cp "$rel/Standalone/$amp" "$root/Standalone/"
  chmod 755 "$root/CLAP/$amp.clap" "$root/Standalone/$amp"
  # The menu entry's icon: the amp's own, from the tree (CMakeLists.txt's ICON_BIG on Linux).
  icon="$HERE/../resources/icon/$amp-1024.png"
  if [ -f "$icon" ]; then cp "$icon" "$root/icons/$amp.png"; fi
done

cp -R "$FACTORY" "$root/Factory"
cp "$HERE/linux/install.sh" "$HERE/linux/uninstall.sh" "$root/"
chmod 755 "$root/install.sh" "$root/uninstall.sh"
printf 'name=%s\nversion=%s\namps=%s\n' "$FAMILY" "$VERSION" "$AMPS" > "$root/release.ini"

tar -C "$work" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "built $OUT/$NAME.tar.gz"
