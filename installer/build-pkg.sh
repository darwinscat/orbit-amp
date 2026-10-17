#!/usr/bin/env bash
# Build a macOS installer package (.pkg): the VST3 + AU plug-ins into the system plug-in folders,
# and the Standalone app into /Applications. This produces an UNSIGNED .pkg — release.sh then
# productsigns it (Developer ID Installer) + notarizes + staples. The bundles it copies are
# expected to be already codesigned (release.sh signs them before calling this).
#
# Usage: build-pkg.sh <version> <release-artefacts-dir> <out-dir> [product]
#   e.g. installer/build-pkg.sh 0.1.0 build-release/OrbitAmp_artefacts/Release dist
#        installer/build-pkg.sh 0.2.0 build/CatpitAmp_artefacts/Release dist CatpitAmp
# The product (OrbitAmp when left out) names the bundles, the package and its identifier
# (com.darwinscat.<product in lower case>, the same as the product's BUNDLE_ID).
set -euo pipefail

VERSION="${1:?usage: build-pkg.sh <version> <release-dir> <out-dir> [product]}"
REL="${2:?missing release artefacts dir}"
OUT="${3:?missing output dir}"
PRODUCT="${4:-OrbitAmp}"
ID="com.darwinscat.$(printf '%s' "$PRODUCT" | tr '[:upper:]' '[:lower:]')"

# Stage the FULL absolute install tree and use --install-location "/" so plug-ins and the app can
# land in DIFFERENT roots from one pkgbuild: plug-ins under /Library/Audio/Plug-Ins, the Standalone
# under /Applications. Factory device packs need no payload of their own — they ride INSIDE the
# binary, and the amp lays them out in the family's Factory folder when it first starts.
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
PLUG="$stage/Library/Audio/Plug-Ins"
mkdir -p "$PLUG/VST3" "$PLUG/Components" "$stage/Applications" "$OUT"

cp -R "$REL/VST3/$PRODUCT.vst3"    "$PLUG/VST3/"
cp -R "$REL/AU/$PRODUCT.component" "$PLUG/Components/"
# CLAP is a bundle dir on macOS, like VST3 — lands in /Library/Audio/Plug-Ins/CLAP.
if [ -d "$REL/CLAP/$PRODUCT.clap" ]; then
  mkdir -p "$PLUG/CLAP"
  cp -R "$REL/CLAP/$PRODUCT.clap"  "$PLUG/CLAP/"
fi
# Standalone app → /Applications (already signed + stapled by release.sh).
if [ -d "$REL/Standalone/$PRODUCT.app" ]; then
  cp -R "$REL/Standalone/$PRODUCT.app" "$stage/Applications/"
fi

pkgbuild \
  --root "$stage" \
  --install-location "/" \
  --identifier "$ID" \
  --version "$VERSION" \
  "$OUT/$PRODUCT-$VERSION-macOS.pkg"

echo "built $OUT/$PRODUCT-$VERSION-macOS.pkg"
