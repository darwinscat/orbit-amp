#!/usr/bin/env bash
# THE GATES, run the one way every runner runs them — the pull request's CI, the family's release on every
# system, the local release. The list is tests/gates.txt; CMake reads it at configure time and writes, per
# configuration, what this script needs to run each gate: its name, the binary exactly where THIS build put
# it, and its environment (<build>/gates/<config>.txt). `--target Family_Gates` builds them all.
#
# Usage: tools/run-gates.sh <build-dir> [config]          (config: Release unless said)
#
# EVERY GATE IN A HOME OF ITS OWN, made empty for it under <build>/gates/: HOME, and on the Mac
# CFFIXED_USER_HOME, which is where Foundation looks. A gate plays an amp starting for the first time, and
# an amp starting lays its set out in the family's Factory folder and gathers IRs into the family's IRs
# folder; in the runner's own home, or on the machine of whoever runs the release, that would be his.
# The XDG_* folders are let go for the same reason: a Linux runner points them into its own home. (Windows
# finds AppData without asking the environment, so there a gate writes into the runner's own profile —
# the workflows run the gates there only on hosted runners, which are thrown away after the job.)
#
# Every gate runs, red or green, and the run fails at the end if any was red: the ones after the first red
# have something to say too.
set -euo pipefail

BUILD="${1:?usage: run-gates.sh <build-dir> [config]}"
CONFIG="${2:-Release}"
LIST="$BUILD/gates/$CONFIG.txt"
[ -f "$LIST" ] || { echo "run-gates.sh: no $LIST — configure $BUILD with CMake first (it is written from tests/gates.txt)" >&2; exit 1; }
LIST="$(cd "$(dirname "$LIST")" && pwd)/$(basename "$LIST")"
GATES="$(dirname "$LIST")"

windows=false
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=true ;; esac

# Why a gate that would not even start did not, on Windows. 127 out of bash, from a file that
# demonstrably exists and printed nothing, is bash's exec emulation giving up — it cannot tell us WHY.
# Windows can. Run it again through cmd, which reports the real NTSTATUS: 0xC0000135 is a DLL the loader
# could not find, 0xC0000005 an access violation, and a small number is the program's own answer. THREE
# WAYS IN, because a round proved the binary itself healthy — a valid PE32+ console image with every import
# resolved — so what failed was how bash reached it. `%ERRORLEVEL%` inside a cmd string is expanded BEFORE
# the command runs, so an earlier probe reported a stale zero; the status is taken from the launcher here
# instead. EVERY probe runs: `set -e` once killed a round on the first one, so the rest never spoke — which
# is how two rounds went by learning one fact each.
windows_probes() {
  local bin="$1" win
  set +e
  win="$(cygpath -w "$bin")"
  echo "   --- the file itself ---"
  ls -l "$bin"; stat -c '%s bytes' "$bin" 2>/dev/null
  echo "   --- msys path ---";    "$bin";         echo "   status: $?"
  echo "   --- windows path ---"; "$win";         echo "   status: $?"
  echo "   --- through cmd ---";  cmd //c "$win"; echo "   status: $?"

  # THE FULL 32-BIT CODE. bash and cmd both hand back a byte, and a byte cannot tell the program's own 127
  # from the low byte of an NTSTATUS — 0xC0000xxx means the process died before it could answer.
  echo "   --- raw exit code, from PowerShell ---"
  GATEBIN="$win" powershell -NoProfile -Command '& { & $env:GATEBIN; Write-Host ("raw exit: {0} = 0x{0:X8}" -f $LASTEXITCODE) }' 2>&1 | tail -6

  echo "   --- subsystem ---";    file "$bin" 2>&1 | head -2
  set -e
}

# The gates SAY WHAT THEY FOUND and what they printed. Swallowing stdout into /dev/null turned every
# failure into a bare exit code — 127 read as "no such command" for two days when `main()` returns the
# number of failed checks, so it may equally have been 127 of them. A gate whose output is thrown away can
# only ever tell you that something is wrong.
run_gate() {   # run_gate <name> <binary> [VAR=value ...]
  local name="$1" bin="$2"; shift 2
  local log="$GATES/$name.log" home code=0 started=$SECONDS
  echo "-- gate: $name"
  if [ ! -f "$bin" ]; then
    echo "   NOT FOUND: $bin — was it built? (cmake --build $BUILD --target Family_Gates)"; return 1
  fi
  echo "   $bin${*:+  ($*)}"
  home="$(mktemp -d "$GATES/home-$name.XXXXXX")"
  mkdir -p "$home/Library/Application Support"

  # Both streams, and to a FILE: printf to a pipe is block-buffered, so a process that dies before it
  # flushes tells you nothing at all — which is exactly what `chain` once did. The status is captured on
  # the line that RUNS it: after an `if`, `$?` is the status of the `if` itself — 0 when the condition was
  # false and there is no else — which once reported a crashed gate as "exited 0".
  (
    cd "$home"
    unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_STATE_HOME
    export HOME="$home" CFFIXED_USER_HOME="$home"
    for kv in "$@"; do export "${kv?}"; done
    exec "$bin"
  ) > "$log" 2>&1 || code=$?
  cat "$log"
  if [ "$code" -eq 0 ]; then
    echo "   ok ($((SECONDS - started)) s)"; return 0
  fi
  echo "   ^ gate $name exited $code ($((SECONDS - started)) s)"
  if $windows; then windows_probes "$bin"; fi
  return 1
}

total=0; red=()
while IFS=$'\t' read -r name bin env || [ -n "${name:-}" ]; do
  [ -n "$name" ] || continue
  total=$((total + 1))
  # shellcheck disable=SC2086   # the environment is VAR=value words, split on purpose
  run_gate "$name" "$bin" $env || red+=("$name")
done < "$LIST"

echo
if [ "${#red[@]}" -gt 0 ]; then
  echo "== gates: ${#red[@]} of $total red — ${red[*]}"
  exit 1
fi
echo "== gates: all $total green"
