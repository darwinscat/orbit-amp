#!/usr/bin/env bash
# Build a macOS installer package (.pkg): the VST3 + AU plug-ins into the system plug-in folders,
# and the Standalone app into /Applications. This produces an UNSIGNED .pkg — release.sh then
# productsigns it (Developer ID Installer) + notarizes + staples. The bundles it copies are
# expected to be already codesigned (release.sh signs them before calling this).
#
# Usage: build-pkg.sh <version> <release-artefacts-dir> <out-dir>
#   e.g. installer/build-pkg.sh 0.1.0 build-release/OrbitAmp_artefacts/Release dist
set -euo pipefail

VERSION="${1:?usage: build-pkg.sh <version> <release-dir> <out-dir>}"
REL="${2:?missing release artefacts dir}"
OUT="${3:?missing output dir}"

# Stage the FULL absolute install tree and use --install-location "/" so plug-ins and the app can
# land in DIFFERENT roots from one pkgbuild: plug-ins under /Library/Audio/Plug-Ins, the Standalone
# under /Applications. Factory device packs need no payload of their own — they ride INSIDE the
# bundles (Contents/Resources/Devices, put there at build time).
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
PLUG="$stage/Library/Audio/Plug-Ins"
mkdir -p "$PLUG/VST3" "$PLUG/Components" "$stage/Applications" "$OUT"

cp -R "$REL/VST3/OrbitAmp.vst3"    "$PLUG/VST3/"
cp -R "$REL/AU/OrbitAmp.component" "$PLUG/Components/"
# CLAP is a bundle dir on macOS, like VST3 — lands in /Library/Audio/Plug-Ins/CLAP.
if [ -d "$REL/CLAP/OrbitAmp.clap" ]; then
  mkdir -p "$PLUG/CLAP"
  cp -R "$REL/CLAP/OrbitAmp.clap"  "$PLUG/CLAP/"
fi
# Standalone app → /Applications (already signed + stapled by release.sh).
if [ -d "$REL/Standalone/OrbitAmp.app" ]; then
  cp -R "$REL/Standalone/OrbitAmp.app" "$stage/Applications/"
fi

pkgbuild \
  --root "$stage" \
  --install-location "/" \
  --identifier "com.darwinscat.orbitamp" \
  --version "$VERSION" \
  "$OUT/OrbitAmp-$VERSION-macOS.pkg"

echo "built $OUT/OrbitAmp-$VERSION-macOS.pkg"
