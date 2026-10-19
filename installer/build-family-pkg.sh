#!/usr/bin/env bash
# The family's macOS installer: ONE .pkg, "Darwin's Cat Amps <version>", that places every amp of the
# release (installer/family.ini) in one go. Each amp × format is a component package of its own, so
# Customize shows every amp with its formats under it — all ticked; the player unticks what he does
# not want.
#
#   VST3        → /Library/Audio/Plug-Ins/VST3/<Amp>.vst3              com.darwinscat.<amp>.vst3
#   AU          → /Library/Audio/Plug-Ins/Components/<Amp>.component   com.darwinscat.<amp>.au
#   CLAP        → /Library/Audio/Plug-Ins/CLAP/<Amp>.clap              com.darwinscat.<amp>.clap
#   Standalone  → /Applications/<Amp>.app                             com.darwinscat.<amp>.standalone
#   the data    → /Library/Application Support/Darwin's Cat/Factory    com.darwinscat.amps.factory
#
# THE AMPS' DATA is a component of its own, chosen and greyed out: the packs, the cabinets and the faces'
# pictures every amp plays, ONE copy of each (src/device/FactorySet.h). No amp carries any of it in its
# binary, so a player cannot untick it. It is the folder of the release's amps alone, as the build put it
# together (<build>/Shipped/Factory, CMakeLists.txt: THE FACTORY FOLDER), each amp's list with it. Every
# amp keeps its identity — bundle id, plug-in and manufacturer codes — so a DAW session finds the same
# plug-in in the same place.
#
# Usage: build-family-pkg.sh <version> <build-dir> <out-dir>
#   e.g. installer/build-family-pkg.sh 0.8.0 build dist
# <build-dir> is the CMake build dir: each amp's bundles are read from
# <build-dir>/<Amp>_artefacts/Release/{VST3,AU,CLAP,Standalone}, and an amp missing one stops the
# build; the data from <build-dir>/Shipped/Factory, which must hold every amp's list. The bundles are expected to be codesigned and stapled already (sign-family-macos.sh). The .pkg
# is signed only when INSTALLER_IDENTITY names a Developer ID Installer identity; without it, it is built
# unsigned, and productsign can sign it later.
#
# SIGNED BY productsign, NOT BY productbuild --sign. The package is built unsigned and productsign signs
# it, as every release before did. On the signing runner, which has no one at its screen, productbuild's
# own signing cannot reach the Installer key ("SignData failed … CSSMERR_CSP_NO_USER_INTERACTION") where
# productsign can, from the same unlocked keychain.
set -euo pipefail

VERSION="${1:?usage: build-family-pkg.sh <version> <build-dir> <out-dir>}"
BUILD="${2:?missing build dir}"
OUT="${3:?missing output dir}"
INI="$(cd "$(dirname "$0")" && pwd)/family.ini"

FAMILY="$(sed -n 's/^name=//p' "$INI")"
AMPS="$(sed -n 's/^amps=//p' "$INI")"
if [ -z "$FAMILY" ] || [ -z "$AMPS" ]; then
  echo "build-family-pkg.sh: no name or no amps in $INI" >&2; exit 1
fi
FORMATS="vst3 au clap standalone"

format() {   # format <f> — the artefact folder, bundle suffix, install folder and title of one format
  case "$1" in
    vst3)       DIR=VST3;       EXT=vst3;      DEST=/Library/Audio/Plug-Ins/VST3;       TITLE="VST3 plug-in" ;;
    au)         DIR=AU;         EXT=component; DEST=/Library/Audio/Plug-Ins/Components; TITLE="Audio Unit" ;;
    clap)       DIR=CLAP;       EXT=clap;      DEST=/Library/Audio/Plug-Ins/CLAP;       TITLE="CLAP plug-in" ;;
    standalone) DIR=Standalone; EXT=app;       DEST=/Applications;                      TITLE="Standalone app" ;;
  esac
}

# Everything is found before anything is built: a family release with a hole in it is not one. A
# bundle is there when its binary is — a build that skipped a format can leave the bundle's folders
# behind with nothing in them, and that would install as a plug-in no host can load.
for amp in $AMPS; do
  for f in $FORMATS; do
    format "$f"
    b="$BUILD/${amp}_artefacts/Release/$DIR/$amp.$EXT"
    [ -s "$b/Contents/MacOS/$amp" ] || { echo "build-family-pkg.sh: no binary in $b" >&2; exit 1; }
  done
done
FACTORY="$BUILD/Shipped/Factory"
for amp in $AMPS; do
  [ -s "$FACTORY/sets/$amp.txt" ] || { echo "build-family-pkg.sh: no $FACTORY/sets/$amp.txt — build Family_All" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/components" "$OUT"

# The amp's own installers of up to 0.7.0 placed all its formats under ONE receipt, named like
# its bundle id. Each format has a receipt of its own now, and installing any of them retires the
# old one: the files it listed are this installer's, or are left as they were when the player
# unticked their format. Forgetting a receipt touches no file; an amp that never had one skips it.
scripts_for() {   # scripts_for <legacy receipt id> — prints the folder holding its postinstall
  local d="$work/scripts/$1"
  mkdir -p "$d"
  cat > "$d/postinstall" <<EOF
#!/bin/sh
# A family installer's format of this amp is in place: the amp's old single receipt is retired.
legacy="$1"
if /usr/sbin/pkgutil --volume "\$3" --pkg-info "\$legacy" >/dev/null 2>&1; then
  /usr/sbin/pkgutil --volume "\$3" --forget "\$legacy" >/dev/null 2>&1 || true
fi
exit 0
EOF
  chmod 755 "$d/postinstall"
  printf '%s' "$d"
}

outline=""; choices=""; refs=""

# The data first, as the folder its install location holds — with no bundle in it, nothing to pin.
fid="com.darwinscat.amps.factory"
mkdir -p "$work/stage/$fid"
ditto --norsrc --noextattr --noacl "$FACTORY" "$work/stage/$fid/Factory"
pkgbuild --quiet \
  --root "$work/stage/$fid" \
  --install-location "/Library/Application Support/Darwin's Cat" \
  --identifier "$fid" \
  --version "$VERSION" \
  "$work/components/$fid.pkg"
outline+="        <line choice=\"factory\"/>"$'\n'
choices+="    <choice id=\"factory\" title=\"The amps' data\" description=\"The packs, cabinets and pictures every amp plays, in /Library/Application Support/Darwin's Cat/Factory. Always installed.\" start_selected=\"true\" enabled=\"false\">"$'\n'
choices+="        <pkg-ref id=\"$fid\"/>"$'\n'
choices+="    </choice>"$'\n'
refs+="    <pkg-ref id=\"$fid\" version=\"$VERSION\" onConclusion=\"none\">$fid.pkg</pkg-ref>"$'\n'
for amp in $AMPS; do
  low="$(printf '%s' "$amp" | tr '[:upper:]' '[:lower:]')"
  legacy="com.darwinscat.$low"
  scripts="$(scripts_for "$legacy")"
  rel="$BUILD/${amp}_artefacts/Release"
  ampver="$(plutil -extract CFBundleShortVersionString raw "$rel/VST3/$amp.vst3/Contents/Info.plist" 2>/dev/null || true)"

  outline+="        <line choice=\"$low\">"$'\n'
  choices+="    <choice id=\"$low\" title=\"$amp\" description=\"$amp${ampver:+ $ampver}: VST3, Audio Unit, CLAP and the standalone app.\" start_selected=\"true\"/>"$'\n'

  for f in $FORMATS; do
    format "$f"
    id="$legacy.$f"
    stage="$work/stage/$id"
    mkdir -p "$stage"
    # The bundle as built, without the extended attributes the way here hung on it (a quarantine
    # from a downloaded artifact, a Finder tag): the package would carry them onto the player's disk.
    ditto --norsrc --noextattr --noacl "$rel/$DIR/$amp.$EXT" "$stage/$amp.$EXT"

    # Not relocatable: left to itself, Installer looks for a bundle with the same id anywhere on the
    # disk — a copy in ~/Library/Audio/Plug-Ins, a build folder — and updates THAT one instead of
    # placing the bundle here. Every bundle the component holds is pinned to its folder.
    plist="$work/$id.plist"
    pkgbuild --analyze --root "$stage" "$plist" > /dev/null
    i=0
    while plutil -extract "$i" xml1 -o /dev/null "$plist" 2>/dev/null; do
      plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
      i=$((i + 1))
    done

    pkgbuild --quiet \
      --root "$stage" \
      --component-plist "$plist" \
      --install-location "$DEST" \
      --scripts "$scripts" \
      --identifier "$id" \
      --version "$VERSION" \
      "$work/components/$id.pkg"

    outline+="            <line choice=\"$low.$f\"/>"$'\n'
    choices+="    <choice id=\"$low.$f\" title=\"$TITLE\" description=\"$DEST/$amp.$EXT\" start_selected=\"true\">"$'\n'
    choices+="        <pkg-ref id=\"$id\"/>"$'\n'
    choices+="    </choice>"$'\n'
    refs+="    <pkg-ref id=\"$id\" version=\"$VERSION\" onConclusion=\"none\">$id.pkg</pkg-ref>"$'\n'
  done
  outline+="        </line>"$'\n'
done

# Customize is offered, not forced: Install with nothing touched places everything, and the data whatever
# is unticked.
cat > "$work/Distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$FAMILY $VERSION</title>
    <product id="com.darwinscat.amps" version="$VERSION"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
    <choices-outline>
$outline    </choices-outline>
$choices$refs</installer-gui-script>
EOF

PKG="$OUT/darwinscat-amps-$VERSION-macos.pkg"
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
  productbuild --distribution "$work/Distribution.xml" --package-path "$work/components" "$work/unsigned.pkg"
  productsign --sign "$INSTALLER_IDENTITY" "$work/unsigned.pkg" "$PKG"
else
  productbuild --distribution "$work/Distribution.xml" --package-path "$work/components" "$PKG"
fi
echo "built $PKG"
