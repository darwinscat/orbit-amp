#!/usr/bin/env bash
# The family's macOS release, locally — the release workflow's macOS job (.github/workflows/release.yml),
# run on this machine with the same steps, in the same order, through the same scripts:
#
#   1. a FRESH build directory with every local sibling override switched OFF — everything arrives by
#      pinned tag from GitHub. That is the release gate: the shipped build must not depend on whatever
#      happens to be checked out on this machine.
#   2. Family_All (universal arm64 + x86_64) — every format of every amp installer/family.ini names — and
#      the gates, the one list (tests/gates.txt), run by tools/run-gates.sh, each in a home of its own.
#   3. installer/sign-family-macos.sh: codesign every bundle (Developer ID Application, hardened runtime,
#      the standalones with the audio-input entitlement), notarize them in one submission and staple each;
#      the .zip; the .pkg — signed (Developer ID Installer), notarized, stapled, validated.
#   4. SHA256SUMS over what ships.
#
# Usage: installer/release.sh              (the version is the family's, from CMakeLists.txt — version.sh)
# Needs: Developer ID Application + Installer identities in the login keychain and a notarytool
# keychain profile (default loki-notary; APP_IDENTITY / INSTALLER_IDENTITY / NOTARY_PROFILE override).
set -euo pipefail
cd "$(dirname "$0")/.."

export APP_IDENTITY="${APP_IDENTITY:-Developer ID Application}"
export INSTALLER_IDENTITY="${INSTALLER_IDENTITY:-Developer ID Installer}"
export NOTARY_PROFILE="${NOTARY_PROFILE:-loki-notary}"
BUILD=build-release

VER="$(installer/version.sh)"
echo "== Darwin's Cat Amps $VER — release build =="

# 1. Configure clean: pins only. Empty *_DIR / *_SRC switch every local-checkout escape hatch off.
rm -rf "$BUILD"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DORBITAMP_JUCE_DIR= \
  -DORBITAMP_FCORE_DIR= \
  -DORBITAMP_GUITAR_DIR= \
  -DORBITAMP_APPKIT_DIR= \
  -DORBITAMP_NAM_CORE_SRC= \
  -DORBITAMP_NAMZ_SRC= \
  -DORBITAMP_COPY_PLUGIN=OFF \
  -DORBITAMP_DEV_FACTORY=OFF        # a released amp reads the installed factory folder, never build/Factory

# 2. Build the family and its gates, then run them — a release with a red gate is not a release.
cmake --build "$BUILD" --parallel --target Family_All Family_Gates
tools/run-gates.sh "$BUILD" Release

# 3. Sign, notarize, staple; the .zip and the .pkg.
rm -rf dist
installer/sign-family-macos.sh "$VER" "$BUILD" dist

# 4. Checksums beside the artifacts.
( cd dist && shasum -a 256 darwinscat-amps-* > SHA256SUMS )
echo "== done: dist/ =="
ls -l dist
