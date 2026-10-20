#!/usr/bin/env bash
# The family's macOS installer: ONE .pkg, "Darwin's Cat Amps <version>", that places every amp of the
# release (installer/family.ini) in one go. The plug-ins are ONE bundle per format for the whole family,
# every amp inside it under the plug-in ids it always had; the standalone apps are one per amp. Each is a
# component package of its own, so Customize shows the three plug-in formats and, under "Standalone
# apps", every amp's app — all ticked; the player unticks what they do not want.
#
#   VST3        → /Library/Audio/Plug-Ins/VST3/<bundle>.vst3              com.darwinscat.amps.vst3
#   AU          → /Library/Audio/Plug-Ins/Components/<bundle>.component   com.darwinscat.amps.au
#   CLAP        → /Library/Audio/Plug-Ins/CLAP/<bundle>.clap              com.darwinscat.amps.clap
#   Standalone  → /Applications/<Amp>.app                                 com.darwinscat.<amp>.standalone
#   the data    → /Library/Application Support/Darwin's Cat/Factory       com.darwinscat.amps.factory
#
# <bundle> is family.ini's `bundle=` (Darwin's Cat Amps), built by its `bundle_target=`.
#
# THE AMPS' DATA is a component of its own, chosen and greyed out: the packs, the cabinets and the faces'
# pictures every amp plays, ONE copy of each (src/device/FactorySet.h). No amp carries any of it in its
# binary, so a player cannot untick it. It is the folder of the release's amps alone, as the build put it
# together (<build>/Shipped/Factory, CMakeLists.txt: THE FACTORY FOLDER), each amp's list with it. Every
# amp keeps its plug-in ids — plug-in and manufacturer codes — so a DAW session finds the same plug-in.
#
# THE PER-AMP PLUG-INS OF EARLIER RELEASES GO. Up to 0.8.0 every amp was a bundle of its own in every
# format (OrbitAmp.vst3, CatpitAmp.component …, family.ini's `old_bundles=`), with the same ids the family's
# bundle now carries: a host that found both would list every amp twice. So each plug-in format's
# postinstall, once the family's bundle is in place, removes that format's old bundles from the one folder
# those installers placed them in — by their exact names, never a pattern, never anywhere else (a copy in
# ~/Library is the player's) — and forgets the receipts that listed them. A format left unticked leaves its
# old bundles as they were.
#
# Usage: build-family-pkg.sh <version> <build-dir> <out-dir>
#   e.g. installer/build-family-pkg.sh 0.9.0 build dist
# <build-dir> is the CMake build dir: the plug-ins are read from
# <build-dir>/<bundle_target>_artefacts/Release/{VST3,AU,CLAP}, each amp's app from
# <build-dir>/<Amp>_artefacts/Release/Standalone, and a bundle missing stops the build; the data from
# <build-dir>/Shipped/Factory, which must hold every amp's list. The bundles are expected to be codesigned
# and stapled already (sign-family-macos.sh). The .pkg is signed only when INSTALLER_IDENTITY names a
# Developer ID Installer identity; without it, it is built unsigned, and productsign can sign it later.
#
# EVERY COMPONENT IS COMPRESSED THE NEWEST WAY Installer reads (`pkgbuild --compression latest`), which
# needs macOS 11 to open it — the amps' own oldest (CMAKE_OSX_DEPLOYMENT_TARGET). productbuild keeps each
# component as it was built.
#
# SIGNED BY productsign, NOT BY productbuild --sign. The package is built unsigned and productsign signs
# it, as every release before did. On the signing runner, which has no one at its screen, productbuild's
# own signing cannot reach the Installer key ("SignData failed … CSSMERR_CSP_NO_USER_INTERACTION") where
# productsign can, from the same unlocked keychain.
set -euo pipefail
set -f   # no pathname expansion: family.ini's words are names, never patterns

VERSION="${1:?usage: build-family-pkg.sh <version> <build-dir> <out-dir>}"
BUILD="${2:?missing build dir}"
OUT="${3:?missing output dir}"
INI="$(cd "$(dirname "$0")" && pwd)/family.ini"
die() { echo "build-family-pkg.sh: $*" >&2; exit 1; }
ini() { sed -n "s/^$1=//p" "$INI" | tr -d '\r'; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
listing() {   # listing <words> — "A", "A and B", "A, B and C"
  local out="" n=$# i=0 w
  for w in "$@"; do
    i=$((i + 1))
    if [ $i -eq 1 ]; then out="$w"; elif [ $i -eq $n ]; then out+=" and $w"; else out+=", $w"; fi
  done
  printf '%s' "$out"
}

FAMILY="$(ini name)"
AMPS="$(ini amps)"
BUNDLE="$(ini bundle)"
TARGET="$(ini bundle_target)"
OLD="$(ini old_bundles)"
if [ -z "$FAMILY" ] || [ -z "$AMPS" ] || [ -z "$BUNDLE" ] || [ -z "$TARGET" ]; then
  die "no name, amps, bundle or bundle_target in $INI"
fi
# The names the postinstalls remove are plain words, or nothing of theirs is written: a space, a slash or a
# pattern character there would reach further than one bundle.
for old in $OLD; do
  case "$old" in ''|.*|*[!A-Za-z0-9_-]*) die "old_bundles: '$old' is not a plain name" ;; esac
done
PLUGINS="vst3 au clap"
COMPRESS=(--compression latest --min-os-version 11.0)   # see above

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
PLUGS="$BUILD/${TARGET}_artefacts/Release"
for f in $PLUGINS; do
  format "$f"
  b="$PLUGS/$DIR/$BUNDLE.$EXT"
  [ -s "$b/Contents/MacOS/$BUNDLE" ] || die "no binary in $b"
done
for amp in $AMPS; do
  b="$BUILD/${amp}_artefacts/Release/Standalone/$amp.app"
  [ -s "$b/Contents/MacOS/$amp" ] || die "no binary in $b"
done
FACTORY="$BUILD/Shipped/Factory"
for amp in $AMPS; do
  [ -s "$FACTORY/sets/$amp.txt" ] || die "no $FACTORY/sets/$amp.txt — build Family_All"
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/components" "$OUT"

# The amp's own installers of up to 0.7.0 placed all its formats under ONE receipt, named like its
# bundle id, com.darwinscat.<amp>. Installing any component of this package retires it: the files it
# listed are this installer's, or are left as they were when the player unticked them. Forgetting a
# receipt touches no file; an amp that never had one skips it.
standalone_scripts() {   # standalone_scripts <legacy receipt id> — prints the folder holding its postinstall
  local d="$work/scripts/$1"
  mkdir -p "$d"
  cat > "$d/postinstall" <<EOF
#!/bin/sh
# A family installer's app of this amp is in place: the amp's old single receipt is retired.
legacy="$1"
if /usr/sbin/pkgutil --volume "\$3" --pkg-info "\$legacy" >/dev/null 2>&1; then
  /usr/sbin/pkgutil --volume "\$3" --forget "\$legacy" >/dev/null 2>&1 || true
fi
exit 0
EOF
  chmod 755 "$d/postinstall"
  printf '%s' "$d"
}

# One plug-in format's postinstall (format "$f" set): the old per-amp bundles of that format go from the
# folder the family's bundle was just placed in, and the receipts that listed them — the 0.8.0 family's
# com.darwinscat.<amp>.<format> and the old single com.darwinscat.<amp>. $3 is the volume Installer installs
# on, "/" for the startup disk; the folder is that volume's, so the script can be tried on a folder that
# stands for one (`postinstall - - /tmp/fake-volume`). Without a volume it does nothing.
plugin_scripts() {   # plugin_scripts <format> — prints the folder holding its postinstall
  local d="$work/scripts/amps.$1" names="" receipts="" old low
  for old in $OLD; do
    low="$(lower "$old")"
    names+=" '$old.$EXT'"
    receipts+=" $low.$1 $low"
  done
  mkdir -p "$d"
  cat > "$d/postinstall" <<EOF
#!/bin/sh
# The family's $TITLE is in place, every amp in it. The per-amp $TITLE bundles of the family's
# earlier releases go from its folder, by their exact names — a host that found both would list every
# amp twice — and so do the receipts that listed them. Nothing else in the folder is touched.
# \$3 is the volume being installed on ("/" for the startup disk).
[ -n "\${3:-}" ] || exit 0
dir="\${3%/}$DEST"
for old in$names; do
  if [ -e "\$dir/\$old" ] || [ -L "\$dir/\$old" ]; then
    /bin/rm -rf "\$dir/\$old" && echo "removed \$dir/\$old"
  fi
done
for receipt in$receipts; do
  if /usr/sbin/pkgutil --volume "\$3" --pkg-info "com.darwinscat.\$receipt" >/dev/null 2>&1; then
    /usr/sbin/pkgutil --volume "\$3" --forget "com.darwinscat.\$receipt" >/dev/null 2>&1 || true
  fi
done
exit 0
EOF
  chmod 755 "$d/postinstall"
  printf '%s' "$d"
}

# One component package: the bundle as built, without the extended attributes the way here hung on it
# (a quarantine from a downloaded artifact, a Finder tag) — the package would carry them onto the
# player's disk — and NOT RELOCATABLE: left to itself, Installer looks for a bundle with the same id
# anywhere on the disk — a copy in ~/Library/Audio/Plug-Ins, a build folder — and updates THAT one instead
# of placing the bundle here. Every bundle the component holds is pinned to its folder.
component() {   # component <id> <bundle> <install folder> <scripts folder>
  local id="$1" stage="$work/stage/$1" plist="$work/$1.plist" i=0
  mkdir -p "$stage"
  ditto --norsrc --noextattr --noacl "$2" "$stage/$(basename "$2")"
  pkgbuild --analyze --root "$stage" "$plist" > /dev/null
  while plutil -extract "$i" xml1 -o /dev/null "$plist" 2>/dev/null; do
    plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
    i=$((i + 1))
  done
  pkgbuild --quiet "${COMPRESS[@]}" \
    --root "$stage" \
    --component-plist "$plist" \
    --install-location "$3" \
    --scripts "$4" \
    --identifier "$id" \
    --version "$VERSION" \
    "$work/components/$id.pkg"
  refs+="    <pkg-ref id=\"$id\" version=\"$VERSION\" onConclusion=\"none\">$id.pkg</pkg-ref>"$'\n'
}

outline=""; choices=""; refs=""

# The data first, as the folder its install location holds — with no bundle in it, nothing to pin.
fid="com.darwinscat.amps.factory"
mkdir -p "$work/stage/$fid"
ditto --norsrc --noextattr --noacl "$FACTORY" "$work/stage/$fid/Factory"
pkgbuild --quiet "${COMPRESS[@]}" \
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

# The plug-ins: one bundle per format, every amp in it.
# shellcheck disable=SC2086   # the amps are words
every="$(listing $AMPS)"
for f in $PLUGINS; do
  format "$f"
  id="com.darwinscat.amps.$f"
  component "$id" "$PLUGS/$DIR/$BUNDLE.$EXT" "$DEST" "$(plugin_scripts "$f")"
  olds=""
  # shellcheck disable=SC2086
  [ -z "$OLD" ] || olds=" Replaces the $(listing $(for o in $OLD; do printf '%s.%s ' "$o" "$EXT"; done)) of earlier releases there."
  outline+="        <line choice=\"$f\"/>"$'\n'
  choices+="    <choice id=\"$f\" title=\"$TITLE\" description=\"$every in one bundle, $DEST/$BUNDLE.$EXT.$olds\" start_selected=\"true\">"$'\n'
  choices+="        <pkg-ref id=\"$id\"/>"$'\n'
  choices+="    </choice>"$'\n'
done

# The standalone apps: one per amp, under one line.
format standalone
outline+="        <line choice=\"standalone\">"$'\n'
choices+="    <choice id=\"standalone\" title=\"Standalone apps\" description=\"Every amp as an app of its own, in $DEST.\" start_selected=\"true\"/>"$'\n'
for amp in $AMPS; do
  low="$(lower "$amp")"
  app="$BUILD/${amp}_artefacts/Release/Standalone/$amp.app"
  ampver="$(plutil -extract CFBundleShortVersionString raw "$app/Contents/Info.plist" 2>/dev/null || true)"
  id="com.darwinscat.$low.standalone"
  component "$id" "$app" "$DEST" "$(standalone_scripts "com.darwinscat.$low")"
  outline+="            <line choice=\"$low.standalone\"/>"$'\n'
  choices+="    <choice id=\"$low.standalone\" title=\"$amp\" description=\"$amp${ampver:+ $ampver}, in $DEST/$amp.app.\" start_selected=\"true\">"$'\n'
  choices+="        <pkg-ref id=\"$id\"/>"$'\n'
  choices+="    </choice>"$'\n'
done
outline+="        </line>"$'\n'

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
