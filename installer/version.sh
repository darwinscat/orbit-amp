#!/usr/bin/env bash
# THE FAMILY'S VERSION, as the tree says it — CMakeLists.txt's project() VERSION, the number every amp shows,
# stamps its bundles with and compares the releases against — and the guard a release passes on its tag.
#
#   installer/version.sh            prints it
#   installer/version.sh v0.8.0     prints it, and fails unless the tag (or a bare 0.8.0) says the same
#
# The number in the binaries comes from the tree, not from the tag; a release whose tag said one thing and
# whose binaries another would tell its own players, for ever, that they are out of date — or never tell
# them of the next one. So the release workflow refuses such a tag before it builds anything.
set -euo pipefail
cd "$(dirname "$0")/.."

# A family that keeps a version of its own — the basses, installer/bass.ini's `version=` — is read where FAMILY_INI
# names it; its tags wear its `tag_prefix=` (bass-v0.1.0), which a tag is read without.
INI="${FAMILY_INI:-installer/family.ini}"
ini() { sed -n "s/^$1=//p" "$INI" | tr -d '\r'; }
OWN="$(ini version)"
PREFIX="$(ini tag_prefix)"
if [ -n "$OWN" ]; then
  TREE="$OWN"; SAYS="$INI"
else
  TREE="$(sed -n 's/^project(OrbitAmp VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)[ )].*/\1/p' CMakeLists.txt)"; SAYS="CMakeLists.txt"
fi
if [ -z "$TREE" ]; then
  echo "version.sh: no project(OrbitAmp VERSION X.Y.Z …) in CMakeLists.txt" >&2; exit 1
fi
if [ $# -gt 0 ]; then
  TAG="$1"; TAG="${TAG#"${PREFIX:-v}"}"; TAG="${TAG#v}"
  if [ "$TAG" != "$TREE" ]; then
    echo "version.sh: the tag says '$1', $SAYS says $TREE — bump one or re-tag" >&2; exit 1
  fi
fi
echo "$TREE"
