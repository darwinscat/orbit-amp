#!/bin/sh
# Darwin's Cat Amps for Linux: places the amps of this release where hosts look for them.
#
#   ./install.sh                      asks, plug-in by plug-in and app by app (Enter = yes)
#   ./install.sh --yes                everything, no questions
#   ./install.sh --formats vst3,clap  the plug-ins alone (formats: vst3, clap, standalone)
#   ./install.sh --amps orbitamp,redpawamp   of the apps, those amps' alone
#   ./install.sh --skip catpitamp:standalone
#   ./install.sh --system             for every user, through sudo
#   ./install.sh --dry-run ...        shows what it would do, does nothing
#
# The plug-ins are ONE bundle per format with every amp of the release in it, <bundle> below (release.ini's
# `bundle=`, Darwin's Cat Amps); the standalone apps are one per amp, and --amps and --skip pick among them.
#
#                 for you (the default)                   --system
#   VST3          ~/.vst3/<bundle>.vst3                   /usr/local/lib/vst3/<bundle>.vst3
#   CLAP          ~/.clap/<bundle>.clap                   /usr/lib/clap/<bundle>.clap
#   Standalone    ~/.local/bin/<Amp>                      /usr/local/bin/<Amp>
#                 + a menu entry in ~/.local/share/applications or /usr/local/share/applications
#   the data      ~/.local/share/darwinscat-amps/Factory  /usr/local/share/darwinscat-amps/Factory
#
# THE AMPS' DATA — packs, cabinets, pictures, one copy for every amp — is placed whatever is picked: no
# amp carries any in its binary. The amps look for it in the player's folder, then in every user's.
#
# CLAP names one folder for every user, /usr/lib/clap, and hosts look there and in ~/.clap only.
# Whatever was placed is listed in <share>/darwinscat-amps/installed, and uninstall.sh, copied there
# too, removes exactly that.
#
# THE PER-AMP PLUG-INS OF EARLIER RELEASES — OrbitAmp.vst3, CatpitAmp.clap and the like, release.ini's
# `old_bundles=` — hold the same amps under the same ids, so a host that found them beside the family's
# would list every amp twice. Placing a plug-in format takes that format's old bundles away, by their exact
# names, from the folder it places into — an earlier install.sh or a copy by hand put them there — and,
# with --system, from the same folders of the player who ran it, ~/.vst3 and ~/.clap, which hosts read
# too. Nothing else.
set -eu
set -f   # no pathname expansion: release.ini's words are names, never patterns

here="$(cd "$(dirname "$0")" && pwd)"
ini() { sed -n "s/^$1=//p" "$here/release.ini"; }
FAMILY="$(ini name)"
VERSION="$(ini version)"
AMPS="$(ini amps)"
BUNDLE="$(ini bundle)"
OLD="$(ini old_bundles)"
FORMATS="vst3 clap standalone"

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }
die() { echo "install.sh: $*" >&2; exit 1; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
commas() { printf '%s' "$1" | tr ',' ' '; }

# The names the old bundles are removed by are plain words, or nothing is touched: a slash, a dot or a
# pattern character there would reach further than one bundle.
for old in $OLD; do
  case "$old" in ''|.*|*[!A-Za-z0-9_-]*) die "release.ini: old_bundles names '$old', which is not a plain name" ;; esac
done

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
  case "$s" in
    *:standalone) ;;
    *) die "--skip takes amp:standalone, e.g. catpitamp:standalone — the plug-ins hold every amp, and --formats picks them" ;;
  esac
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

# For every user, the home of the player who ran this: sudo names them, or a dry run is still theirs.
own=""
if [ $system = 1 ]; then
  if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    own="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
  elif [ "$(id -u)" != 0 ]; then
    own="$HOME"
  fi
  case "$own" in /?*) ;; *) own="" ;; esac
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
wanted() {   # wanted <format> — picked by --formats, or --formats not given
  [ -z "$want_formats" ] && return 0
  case " $want_formats " in *" $1 "*) return 0 ;; esac
  return 1
}
chosen=""
for f in vst3 clap; do
  wanted $f || continue
  if [ $interactive = 1 ]; then
    case $f in vst3) t="VST3 plug-in" ;; clap) t="CLAP plug-in" ;; esac
    ask "$t, every amp in it?" || continue
  fi
  chosen="$chosen $f"
done
if wanted standalone; then
  for amp in $AMPS; do
    low="$(lower "$amp")"
    if [ -n "$want_amps" ]; then
      case " $want_amps " in *" $low "*) ;; *) continue ;; esac
    fi
    case " $skips " in *" $low:standalone "*) continue ;; esac
    if [ $interactive = 1 ]; then ask "$amp, the standalone app?" || continue; fi
    chosen="$chosen standalone:$amp"
  done
fi
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

# An earlier release's per-amp plug-ins of a format, once the family's bundle of that format is in place
# (see the top): by their exact names, in the folder this one placed into and, for every user, in the
# same folder of the player who ran it. They leave the list too.
retired=""
retire() {   # retire <folder> <suffix>
  for old in $OLD; do
    p="$1/$old.$2"
    if [ -e "$p" ] || [ -L "$p" ]; then
      echo "            removes $p (an earlier release's)"
      run rm -rf "$p"
    fi
    retired="$retired
$p"
  done
}

# The data first, always: the amps play nothing without it.
echo "Data        $DATA/Factory"
place "$here/Factory" "$DATA"

for pick in $chosen; do
  case $pick in
    vst3)
      echo "VST3        $VST3/$BUNDLE.vst3";  place "$here/VST3/$BUNDLE.vst3" "$VST3"; retire "$VST3" vst3
      if [ -n "$own" ]; then retire "$own/.vst3" vst3; fi ;;
    clap)
      echo "CLAP        $CLAP/$BUNDLE.clap";  place "$here/CLAP/$BUNDLE.clap" "$CLAP"; retire "$CLAP" clap
      if [ -n "$own" ]; then retire "$own/.clap" clap; fi ;;
    standalone:*)
      amp="${pick#standalone:}"
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

# What is placed, for uninstall.sh: merged with what an earlier run placed, less what was taken away.
run mkdir -p "$DATA"
run cp "$here/uninstall.sh" "$DATA/uninstall.sh"
if [ $dry = 0 ]; then
  { [ -f "$DATA/installed" ] && cat "$DATA/installed"; printf '%s\n' "$placed"; } \
    | sed '/^$/d' | sort -u > "$DATA/installed.new"
  if [ -n "$retired" ]; then
    printf '%s\n' "$retired" | sed '/^$/d' > "$DATA/retired.new"
    grep -vxF -f "$DATA/retired.new" "$DATA/installed.new" > "$DATA/installed.kept" || true
    mv "$DATA/installed.kept" "$DATA/installed.new"
    rm -f "$DATA/retired.new"
  fi
  mv "$DATA/installed.new" "$DATA/installed"
  command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
fi

if [ $dry = 1 ]; then echo "(dry run: nothing was placed)"; exit 0; fi
echo "Done. To remove: $DATA/uninstall.sh$( [ $system = 1 ] && echo ' --system' )"
case ":$PATH:" in *":$BIN:"*) ;; *) echo "($BIN is not on your PATH: the menu entries start the apps all the same.)" ;; esac
