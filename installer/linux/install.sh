#!/bin/sh
# Darwin's Cat Amps for Linux: places the amps of this release where hosts look for them.
#
#   ./install.sh                     asks, amp by amp and format by format (Enter = yes)
#   ./install.sh --yes               everything, no questions
#   ./install.sh --amps orbitamp,redpawamp --formats vst3,clap
#   ./install.sh --skip catpitamp:standalone
#   ./install.sh --system            for every user, through sudo
#   ./install.sh --dry-run ...       shows what it would do, does nothing
#
#                 for you (the default)                   --system
#   VST3          ~/.vst3/<Amp>.vst3                      /usr/local/lib/vst3/<Amp>.vst3
#   CLAP          ~/.clap/<Amp>.clap                      /usr/lib/clap/<Amp>.clap
#   Standalone    ~/.local/bin/<Amp>                      /usr/local/bin/<Amp>
#                 + a menu entry in ~/.local/share/applications or /usr/local/share/applications
#
# CLAP names one folder for every user, /usr/lib/clap, and hosts look there and in ~/.clap only.
# Whatever was placed is listed in <share>/darwinscat-amps/installed, and uninstall.sh, copied there
# too, removes exactly that.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
ini() { sed -n "s/^$1=//p" "$here/release.ini"; }
FAMILY="$(ini name)"
VERSION="$(ini version)"
AMPS="$(ini amps)"
FORMATS="vst3 clap standalone"

usage() { sed -n '2,19s/^# \{0,1\}//p' "$0"; }
die() { echo "install.sh: $*" >&2; exit 1; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
commas() { printf '%s' "$1" | tr ',' ' '; }

# The options are read in a function, so the script's own arguments stay whole for sudo below.
system=0; yes=0; dry=0; picked=0
want_amps=""; want_formats=""; skips=""
parse() {
  while [ $# -gt 0 ]; do
    case "$1" in
      --amps)       [ $# -ge 2 ] || die "--amps needs a list"; want_amps="$(lower "$(commas "$2")")"; picked=1; shift ;;
      --formats)    [ $# -ge 2 ] || die "--formats needs a list"; want_formats="$(lower "$(commas "$2")")"; picked=1; shift ;;
      --skip)       [ $# -ge 2 ] || die "--skip needs a list"; skips="$skips $(lower "$(commas "$2")")"; picked=1; shift ;;
      -y|--yes)     yes=1 ;;
      --system)     system=1 ;;
      -n|--dry-run) dry=1 ;;
      -h|--help)    usage; exit 0 ;;
      *)            die "unknown option $1 (--help lists them)" ;;
    esac
    shift
  done
}
parse "$@"

# Names are checked before anything is asked or touched.
for a in $want_amps; do
  ok=0; for amp in $AMPS; do [ "$a" = "$(lower "$amp")" ] && ok=1; done
  [ $ok = 1 ] || die "no amp '$a' in this release (it has: $AMPS)"
done
for f in $want_formats; do
  case " $FORMATS " in *" $f "*) ;; *) die "no format '$f' (formats: $FORMATS)" ;; esac
done
for s in $skips; do
  case "$s" in *:*) ;; *) die "--skip takes amp:format, e.g. catpitamp:standalone" ;; esac
done

# For every user, the whole script runs again through sudo, with the same arguments.
if [ $system = 1 ] && [ $dry = 0 ] && [ "$(id -u)" != 0 ]; then
  echo "Installing for every user: sudo asks for your password."
  exec sudo sh "$0" "$@"
fi

if [ $system = 1 ]; then
  VST3=/usr/local/lib/vst3; CLAP=/usr/lib/clap; BIN=/usr/local/bin
  APPS=/usr/local/share/applications; DATA=/usr/local/share/darwinscat-amps
else
  share="${XDG_DATA_HOME:-$HOME/.local/share}"
  VST3="$HOME/.vst3"; CLAP="$HOME/.clap"; BIN="$HOME/.local/bin"
  APPS="$share/applications"; DATA="$share/darwinscat-amps"
fi

ask() {   # ask <question> — yes unless the answer starts with n
  printf '%s [Y/n] ' "$1"
  answer=""
  read -r answer < /dev/tty || answer=""
  case "$answer" in [nN]*) return 1 ;; *) return 0 ;; esac
}

# Questions only when nothing was chosen on the command line and someone is there to answer them.
interactive=0
if [ $yes = 0 ] && [ $picked = 0 ] && [ -t 0 ] && (: < /dev/tty) 2>/dev/null; then interactive=1; fi

echo "$FAMILY $VERSION"
if [ $dry = 1 ]; then echo "(dry run: where everything would go; nothing is changed)"; fi
chosen=""
for amp in $AMPS; do
  low="$(lower "$amp")"
  if [ -n "$want_amps" ]; then
    case " $want_amps " in *" $low "*) ;; *) continue ;; esac
  fi
  if [ $interactive = 1 ]; then ask "$amp?" || continue; fi
  for f in $FORMATS; do
    if [ -n "$want_formats" ]; then
      case " $want_formats " in *" $f "*) ;; *) continue ;; esac
    fi
    case " $skips " in *" $low:$f "*) continue ;; esac
    if [ $interactive = 1 ]; then
      case $f in vst3) t="VST3 plug-in" ;; clap) t="CLAP plug-in" ;; standalone) t="Standalone app" ;; esac
      ask "  $t?" || continue
    fi
    chosen="$chosen $amp:$f"
  done
done
[ -n "$chosen" ] || { echo "Nothing chosen, nothing installed."; exit 0; }

run() { if [ $dry = 0 ]; then "$@"; fi; }   # a dry run names the places and touches none

# Into place by rename: a running standalone or a plug-in a host has open is replaced, not written
# over, and a bundle folder holds nothing of an older build.
place() {   # place <source> <folder> — copies a file or a bundle folder into <folder>
  name="$(basename "$1")"
  run mkdir -p "$2"
  run rm -rf "$2/.$name.new"
  run cp -R "$1" "$2/.$name.new"
  run rm -rf "$2/$name"
  run mv "$2/.$name.new" "$2/$name"
  placed="$placed
$2/$name"
}

placed=""
for pick in $chosen; do
  amp="${pick%%:*}"; f="${pick#*:}"
  case $f in
    vst3) echo "VST3        $VST3/$amp.vst3";  place "$here/VST3/$amp.vst3" "$VST3" ;;
    clap) echo "CLAP        $CLAP/$amp.clap";  place "$here/CLAP/$amp.clap" "$CLAP" ;;
    standalone)
      echo "Standalone  $BIN/$amp"
      place "$here/Standalone/$amp" "$BIN"
      icon=""
      if [ -f "$here/icons/$amp.png" ]; then
        place "$here/icons/$amp.png" "$DATA/icons"
        icon="$DATA/icons/$amp.png"
      fi
      desktop="$APPS/com.darwinscat.$(lower "$amp").desktop"
      echo "            $desktop"
      run mkdir -p "$APPS"
      if [ $dry = 0 ]; then
        {
          echo "[Desktop Entry]"
          echo "Type=Application"
          echo "Name=$amp"
          echo "Comment=$FAMILY"
          echo "Exec=\"$BIN/$amp\""
          [ -n "$icon" ] && echo "Icon=$icon"
          echo "Terminal=false"
          echo "Categories=AudioVideo;Audio;"
        } > "$desktop"
      fi
      placed="$placed
$desktop" ;;
  esac
done

# What is placed, for uninstall.sh: merged with what an earlier run placed.
run mkdir -p "$DATA"
run cp "$here/uninstall.sh" "$DATA/uninstall.sh"
if [ $dry = 0 ]; then
  { [ -f "$DATA/installed" ] && cat "$DATA/installed"; printf '%s\n' "$placed"; } \
    | sed '/^$/d' | sort -u > "$DATA/installed.new"
  mv "$DATA/installed.new" "$DATA/installed"
  command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
fi

if [ $dry = 1 ]; then echo "(dry run: nothing was placed)"; exit 0; fi
echo "Done. To remove: $DATA/uninstall.sh$( [ $system = 1 ] && echo ' --system' )"
case ":$PATH:" in *":$BIN:"*) ;; *) echo "($BIN is not on your PATH: the menu entries start the apps all the same.)" ;; esac
