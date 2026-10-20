#!/usr/bin/env bash
# THE FAMILY'S macOS RELEASE, from built bundles to what ships. The family's plug-ins — ONE bundle per
# format, every amp of installer/family.ini inside it (family.ini's `bundle=`) — and every amp's standalone
# app are signed (Developer ID, hardened runtime), all of them are notarized in ONE submission and each gets its
# ticket stapled; then the zip (build-family-zip.sh) and the package (build-family-pkg.sh) are made from
# the stapled bundles, and the package is signed, notarized and stapled. The release workflow's macOS job
# runs this on the signing runner, and installer/release.sh runs it here: the same steps, said once.
#
# Usage: sign-family-macos.sh <version> <build-dir> <out-dir>
# Needs, in the environment: APP_IDENTITY (a Developer ID Application identity), INSTALLER_IDENTITY (a
# Developer ID Installer identity) and NOTARY_PROFILE (a notarytool keychain profile) — and a keychain
# they can be used from without a prompt (the workflow unlocks the runner's first).
# Out: <out-dir>/darwinscat-amps-<version>-macos-arm64.zip and …-macos-x86_64.zip (one architecture each,
# build-family-zip.sh), and darwinscat-amps-<version>-macos.pkg (universal).
set -euo pipefail

VER="${1:?usage: sign-family-macos.sh <version> <build-dir> <out-dir>}"
BUILD="${2:?missing build dir}"
OUT="${3:?missing output dir}"
: "${APP_IDENTITY:?a Developer ID Application identity}"
: "${INSTALLER_IDENTITY:?a Developer ID Installer identity}"
: "${NOTARY_PROFILE:?a notarytool keychain profile}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ini() { sed -n "s/^$1=//p" "$HERE/family.ini" | tr -d '\r'; }
AMPS="$(ini amps)"
BUNDLE="$(ini bundle)"
TARGET="$(ini bundle_target)"
if [ -z "$AMPS" ] || [ -z "$BUNDLE" ] || [ -z "$TARGET" ]; then
  echo "sign-family-macos.sh: no amps, bundle or bundle_target in $HERE/family.ini" >&2; exit 1
fi
mkdir -p "$OUT"

notarize() {   # notarize <file> — submit, wait, and refuse anything but Accepted
  local out
  out="$(xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" --wait 2>&1 | tee /dev/stderr)"
  grep -q "status: Accepted" <<< "$out"
}

# A TICKET TAKES A MOMENT TO REACH THE REPLICA STAPLER ASKS. `notarytool --wait` returns Accepted once the
# ticket is written in Apple's notary database; `stapler` then makes a SEPARATE request to a CloudKit
# endpoint to fetch it, and between those two there is a propagation delay — usually seconds, sometimes
# minutes, longer when Apple's service is having a bad afternoon. It reads as `The staple and validate
# action failed! Error 65`, which means "no such ticket", on a submission that was accepted.
#
# The family staples three plug-in bundles, an app per amp and the package, so it has that many chances to land inside that
# window, and `set -e` would end the release on the first one, after both notarization round trips are
# already paid for. Waiting costs nothing on the day it works, which is most days.
staple_retry() {
  local item="$1" i
  for i in 1 2 3 4 5; do
    xcrun stapler staple "$item" && return 0
    echo "staple: no ticket yet for $item (attempt $i of 5) — waiting for it to propagate"
    sleep 20
  done
  return 1
}

BUNDLES=()
# The plug-ins, one bundle per format for every amp: hardened-runtime sign (they run inside the host → no
# entitlements of their own).
plugs="$BUILD/${TARGET}_artefacts/Release"
lipo -info "$plugs/VST3/$BUNDLE.vst3/Contents/MacOS/$BUNDLE"
for b in "$plugs/VST3/$BUNDLE.vst3" "$plugs/AU/$BUNDLE.component" "$plugs/CLAP/$BUNDLE.clap"; do
  codesign --force --options runtime --timestamp --sign "$APP_IDENTITY" "$b"
  codesign --verify --strict --verbose=1 "$b"
  BUNDLES+=("$b")
done

for amp in $AMPS; do
  rel="$BUILD/${amp}_artefacts/Release"
  lipo -info "$rel/Standalone/$amp.app/Contents/MacOS/$amp"

  # The standalone: its own process → the audio-input entitlement, under the hardened runtime.
  app="$rel/Standalone/$amp.app"
  codesign --force --options runtime --timestamp \
           --entitlements "$HERE/standalone.entitlements" --sign "$APP_IDENTITY" "$app"
  codesign --verify --strict --verbose=1 "$app"
  BUNDLES+=("$app")
done

# ONE SUBMISSION FOR EVERYTHING, AND A TICKET ON EACH BUNDLE.
#
# OrbitAmp's releases once submitted the standalone alone, so only the app came back with a ticket stapled
# to it — the plug-ins in the .zip had none, and the .pkg's own submission staples the PACKAGE, not what is
# inside it. Notarized they were, and Gatekeeper would fetch the verdict over the network on first load; a
# stapled ticket is the same verdict already in the bundle, which is what a machine with no network, or
# Apple having a bad afternoon, needs.
#
# Stapling does not disturb the signature — the ticket lands beside the seal, not inside it — so the
# originals are stapled and everything downstream, the .zip and the .pkg alike, is made from bundles that
# already carry theirs. Every bundle's name is its own — the family's with a format's suffix, or an amp's
# app — so they share one folder.
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
for b in "${BUNDLES[@]}"; do ditto --norsrc --noextattr --noacl "$b" "$stage/bundles/$(basename "$b")"; done
ditto -c -k --norsrc --noextattr --noacl "$stage/bundles" "$stage/bundles.zip"
notarize "$stage/bundles.zip"

for b in "${BUNDLES[@]}"; do
  staple_retry "$b"
  xcrun stapler validate "$b"
  codesign --verify --deep --strict "$b"
done

# What ships: a zip of the stapled bundles for each architecture, thinned after they were notarized, and
# the package, universal — signed as productbuild makes it, then notarized and stapled itself.
"$HERE/build-family-zip.sh" "$VER" "$BUILD" "$OUT" macos-arm64
"$HERE/build-family-zip.sh" "$VER" "$BUILD" "$OUT" macos-x86_64

PKG="$OUT/darwinscat-amps-$VER-macos.pkg"
INSTALLER_IDENTITY="$INSTALLER_IDENTITY" "$HERE/build-family-pkg.sh" "$VER" "$BUILD" "$OUT"
notarize "$PKG"
staple_retry "$PKG"
xcrun stapler validate "$PKG"
spctl -a -vvv -t install "$PKG" || true
