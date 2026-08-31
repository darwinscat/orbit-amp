#!/usr/bin/env bash
# The whole macOS release, locally — this repo drives its release from this script the way
# OrbitCab's release.yml drives it on a runner:
#
#   1. a FRESH build directory with every local sibling override switched OFF — everything
#      arrives by pinned tag from GitHub. That is the release gate: the shipped build must not
#      depend on whatever happens to be checked out on this machine.
#   2. Release build (universal arm64 + x86_64) and the acceptance gates, all of them.
#   3. Codesign (Developer ID Application, hardened runtime): plug-ins plainly, the Standalone
#      with the audio-input entitlement, then notarize + staple the Standalone so it launches
#      cleanly straight from the .zip.
#   4. The .zip of signed bundles; the .pkg — productsign (Developer ID Installer), notarize,
#      staple, validate.
#   5. SHA256SUMS over what ships.
#
# Usage: installer/release.sh              (the version comes from CMakeLists.txt)
# Needs: Developer ID Application + Installer identities in the login keychain and a notarytool
# keychain profile (default loki-notary; NOTARY_PROFILE=... overrides).
set -euo pipefail
cd "$(dirname "$0")/.."

APP_ID="${APP_IDENTITY:-Developer ID Application}"
INST_ID="${INSTALLER_IDENTITY:-Developer ID Installer}"
NOTARY="${NOTARY_PROFILE:-loki-notary}"
BUILD=build-release

VER="$(sed -n 's/^project(OrbitAmp VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VER" ] || { echo "release.sh: no version in CMakeLists.txt" >&2; exit 1; }
echo "== OrbitAmp $VER — release build =="

notarize() {   # notarize <file> — submit and refuse anything but Accepted
  local out
  out="$(xcrun notarytool submit "$1" --keychain-profile "$NOTARY" --wait 2>&1 | tee /dev/stderr)"
  grep -q "status: Accepted" <<< "$out"
}

# 1. Configure clean: pins only. Empty *_DIR / *_SRC switch every local-checkout escape hatch off.
rm -rf "$BUILD"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DORBITAMP_JUCE_DIR= \
  -DORBITAMP_FCORE_DIR= \
  -DORBITAMP_APPKIT_DIR= \
  -DORBITAMP_NAM_CORE_SRC= \
  -DORBITAMP_NAMZ_SRC= \
  -DORBITAMP_COPY_PLUGIN=OFF

# 2. Build everything, then run the gates — a release with a red gate is not a release.
cmake --build "$BUILD" --parallel
for t in captured chain library reverb ribbon; do
  bin="$(find "$BUILD/orbitamp_${t}_test_artefacts" -type f -perm +111 | head -1)"
  echo "-- gate: $t"; "$bin" > /dev/null
done
echo "-- gate: eqlink"; "$BUILD/orbitamp_eqlink_test" > /dev/null
echo "-- gate: tuner";  "$BUILD/orbitamp_tuner_test"  > /dev/null

REL="$BUILD/OrbitAmp_artefacts/Release"
VST3="$REL/VST3/OrbitAmp.vst3"; AU="$REL/AU/OrbitAmp.component"; SA="$REL/Standalone/OrbitAmp.app"
lipo -info "$SA/Contents/MacOS/OrbitAmp"
rm -rf dist stage; mkdir -p dist stage

# 3. Plug-ins: hardened-runtime sign (they run inside the host → no entitlements of their own).
for b in "$VST3" "$AU"; do
  codesign --force --options runtime --timestamp --sign "$APP_ID" "$b"
  codesign --verify --strict --verbose=1 "$b"
  cp -R "$b" stage/
done

# Standalone: its own process → the audio-input entitlement, then notarize + staple the app
# itself (the .pkg's own notarization covers the installed copy; this covers the .zip copy).
codesign --force --options runtime --timestamp \
         --entitlements installer/standalone.entitlements --sign "$APP_ID" "$SA"
codesign --verify --strict --verbose=1 "$SA"
ditto -c -k --keepParent "$SA" dist/_standalone.zip
notarize dist/_standalone.zip
xcrun stapler staple "$SA"
rm -f dist/_standalone.zip
cp -R "$SA" stage/

# 4. What ships: the .zip of signed bundles, and the notarized .pkg.
ditto -c -k stage "dist/OrbitAmp-$VER-macOS.zip"

installer/build-pkg.sh "$VER" "$REL" dist
productsign --sign "$INST_ID" "dist/OrbitAmp-$VER-macOS.pkg" "dist/OrbitAmp-$VER-macOS-signed.pkg"
mv "dist/OrbitAmp-$VER-macOS-signed.pkg" "dist/OrbitAmp-$VER-macOS.pkg"
notarize "dist/OrbitAmp-$VER-macOS.pkg"
xcrun stapler staple "dist/OrbitAmp-$VER-macOS.pkg"
xcrun stapler validate "dist/OrbitAmp-$VER-macOS.pkg"
spctl -a -vvv -t install "dist/OrbitAmp-$VER-macOS.pkg" || true

# 5. Checksums beside the artifacts.
( cd dist && shasum -a 256 OrbitAmp-* > SHA256SUMS )
rm -rf stage
echo "== done: dist/ =="
ls -l dist
