#!/bin/sh
# Darwin's Cat Amps for Linux: removes what install.sh placed, and nothing else.
#
#   ./uninstall.sh              what was installed for you
#   ./uninstall.sh --system     what was installed for every user, through sudo
#   ./uninstall.sh --dry-run    shows what it would remove, removes nothing
#
# It reads the list install.sh keeps (<share>/darwinscat-amps/installed) and removes a path only when
# it lies in one of the folders install.sh places into. The amps' own settings, presets and packs
# (in ~/.config, ~/Documents and the like) are the player's and stay.
set -eu

die() { echo "uninstall.sh: $*" >&2; exit 1; }
system=0; dry=0
for arg in "$@"; do
  case "$arg" in
    --system)     system=1 ;;
    -n|--dry-run) dry=1 ;;
    -h|--help)    sed -n '2,10s/^# \{0,1\}//p' "$0"; exit 0 ;;
    *)            die "unknown option $arg (--help lists them)" ;;
  esac
done

if [ $system = 1 ] && [ $dry = 0 ] && [ "$(id -u)" != 0 ]; then
  echo "Removing what was installed for every user: sudo asks for your password."
  exec sudo sh "$0" "$@"
fi

# The same folders install.sh uses.
if [ $system = 1 ]; then
  VST3=/usr/local/lib/vst3; CLAP=/usr/lib/clap; BIN=/usr/local/bin
  APPS=/usr/local/share/applications; DATA=/usr/local/share/darwinscat-amps
else
  share="${XDG_DATA_HOME:-$HOME/.local/share}"
  VST3="$HOME/.vst3"; CLAP="$HOME/.clap"; BIN="$HOME/.local/bin"
  APPS="$share/applications"; DATA="$share/darwinscat-amps"
fi

list="$DATA/installed"
[ -f "$list" ] || { echo "Nothing to remove: no $list."; exit 0; }

run() { if [ $dry = 0 ]; then "$@"; fi; }   # a dry run names what it would remove and removes none

while IFS= read -r path; do
  case "$(basename "$path")" in ''|.|..) echo "skipped: $path"; continue ;; esac
  case "$path" in
    "$DATA/Factory") ;;                     # the amps' data, which install.sh always places
    *)
      case "$(dirname "$path")" in
        "$VST3"|"$CLAP"|"$BIN"|"$APPS"|"$DATA/icons") ;;
        *) echo "skipped (not a folder install.sh places into): $path"; continue ;;
      esac ;;
  esac
  if [ -e "$path" ] || [ -L "$path" ]; then
    if [ $dry = 1 ]; then echo "would remove $path"; else echo "removing $path"; fi
    run rm -rf "$path"
  fi
done < "$list"

# Then the list, the copy of this script beside it and their folder, and the plug-in folders when
# nothing else is left in them.
run rm -f "$list" "$DATA/uninstall.sh"
for d in "$DATA/icons" "$DATA" "$VST3" "$CLAP"; do
  if [ -d "$d" ]; then run rmdir "$d" 2>/dev/null || true; fi
done
if [ $dry = 0 ] && command -v update-desktop-database > /dev/null 2>&1; then
  update-desktop-database "$APPS" 2>/dev/null || true
fi
echo "Done."
