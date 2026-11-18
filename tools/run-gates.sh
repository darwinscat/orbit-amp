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
# The XDG_* folders are let go for the same reason: a Linux runner points them into its own home. Windows
# finds AppData and Documents without asking the environment, so there the home is named once more, as
# ORBITAMP_GATE_HOME, and a gate reads the player's folders from it (src/UserFolders.h).
#
# Every gate runs, red or green, and the run fails at the end if any was red: the ones after the first red
# have something to say too.
#
# A TIME LIMIT FOR EVERY GATE, AND A WORD WHILE IT RUNS. A gate once hung on a Windows runner for two and a
# half hours, silent — its output goes to a file and is shown when it ends — until the job's own limit
# killed the whole run. Now a gate has ORBITAMP_GATE_TIMEOUT_MIN minutes (30 unless said): past them it is
# killed with everything it started, its output so far is shown, it counts red and the next gate runs.
# Every ORBITAMP_GATE_HEARTBEAT_MIN minutes (5 unless said) a gate still running says so, with the last
# line it printed. 0 turns either off. ORBITAMP_GATE_TIMEOUT_SEC and ORBITAMP_GATE_HEARTBEAT_SEC, in
# seconds, win over the minutes: they are for tools/run-gates-selftest.sh, which proves all of this on
# tiny script gates.
set -euo pipefail

BUILD="${1:?usage: run-gates.sh <build-dir> [config]}"
CONFIG="${2:-Release}"
LIST="$BUILD/gates/$CONFIG.txt"
[ -f "$LIST" ] || { echo "run-gates.sh: no $LIST — configure $BUILD with CMake first (it is written from tests/gates.txt)" >&2; exit 1; }
LIST="$(cd "$(dirname "$LIST")" && pwd)/$(basename "$LIST")"
GATES="$(dirname "$LIST")"

windows=false
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=true ;; esac

seconds_of() {   # seconds_of <minutes variable> <seconds variable> <default minutes>
  local v="${!2:-}" unit=1 from="$2"
  if [ -z "$v" ]; then v="${!1:-$3}" unit=60 from="$1"; fi
  case "$v" in ''|*[!0-9]*) echo "run-gates.sh: $from=$v — a whole number, please (0 turns it off)" >&2; return 1 ;; esac
  echo $((10#$v * unit))
}
limit="$(seconds_of ORBITAMP_GATE_TIMEOUT_MIN ORBITAMP_GATE_TIMEOUT_SEC 30)" || exit 1
beat="$(seconds_of ORBITAMP_GATE_HEARTBEAT_MIN ORBITAMP_GATE_HEARTBEAT_SEC 5)" || exit 1
in_words() { if [ $(($1 % 60)) -eq 0 ]; then echo "$(($1 / 60)) min"; else echo "$1 s"; fi; }

# The last line a gate printed that says something, cut to fit one line of the run's log. A line redrawn
# with carriage returns counts as its newest frame.
last_line() {
  local line
  # shellcheck disable=SC2016   # a sed script, not an expansion
  line="$(tail -n 20 "$1" 2>/dev/null | LC_ALL=C tr '\r' '\n' \
          | LC_ALL=C sed -n '/[^[:space:]]/h; ${x;s/^[[:space:]]*//;p;}' | LC_ALL=C cut -c 1-150)" || true
  echo "${line:-(nothing yet)}"
}

# A gate that ran out of time goes WITH EVERYTHING IT STARTED: it runs in a process group of its own, and
# the whole group is signalled — TERM, then KILL for whatever is still there after three seconds. On
# Windows a native program never sees an MSYS signal, so there the tree is killed the Windows way first:
# taskkill /T from the Windows pid of the subshell the gate runs under, which is the parent of all of it.
kill_gate() {   # kill_gate <pid>
  local pid="$1" i=0
  if $windows && [ -r "/proc/$pid/winpid" ]; then
    taskkill //F //T //PID "$(cat "/proc/$pid/winpid")" > /dev/null 2>&1 || true
  fi
  kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  while kill -0 -- "-$pid" 2>/dev/null && [ "$i" -lt 30 ]; do sleep 0.1 2>/dev/null || sleep 1; i=$((i + 1)); done
  kill -KILL -- "-$pid" 2>/dev/null || true
  kill -KILL "$pid" 2>/dev/null || true
}

# Still running, and still OURS: bash's own table of the jobs it started, not `kill -0`, which says yes to
# whatever process has since been given the same pid.
ours() { jobs -rp | grep -qx "$1"; }

# The gate's own process group no longer hears a Ctrl-C or a cancelled job, so the run passes it on: a gate
# never outlives the run that started it.
gate_pid=
trap '[ -z "$gate_pid" ] || ! ours "$gate_pid" || { kill_gate "$gate_pid"; } 2>/dev/null' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

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
  local log="$GATES/$name.log" ended="$GATES/$name.exit" home code=0 started=$SECONDS pid elapsed polls=0
  local next_beat="$beat" how=ended
  echo "-- gate: $name"
  if [ ! -f "$bin" ]; then
    echo "   NOT FOUND: $bin — was it built? (cmake --build $BUILD --target Family_Gates)"; return 1
  fi
  echo "   $bin${*:+  ($*)}"
  home="$(mktemp -d "$GATES/home-$name.XXXXXX")"
  mkdir -p "$home/Library/Application Support"
  rm -f "$ended"; : > "$log"

  # Both streams, and to a FILE: printf to a pipe is block-buffered, so a process that dies before it
  # flushes tells you nothing at all — which is exactly what `chain` once did. The status is captured on
  # the line that RUNS it: after an `if`, `$?` is the status of the `if` itself — 0 when the condition was
  # false and there is no else — which once reported a crashed gate as "exited 0".
  # IN THE BACKGROUND, in a process group of its own (set -m), so the loop below can watch the clock and
  # kill it with all it started. As it ends, its status is written to <name>.exit, and the loop waits for
  # that file rather than for the pid alone: the pid of a gate that ended goes back to the system.
  set -m
  (
    code=0
    (
      cd "$home"
      unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_STATE_HOME
      export HOME="$home" CFFIXED_USER_HOME="$home"
      if $windows; then export ORBITAMP_GATE_HOME="$(cygpath -m "$home")"; fi
      for kv in "$@"; do export "${kv?}"; done
      exec "$bin"
    ) < /dev/null > "$log" 2>&1 || code=$?
    echo "$code" > "$ended"
    exit "$code"
  ) &
  pid=$! gate_pid=$!
  set +m

  # A tenth of a second at first, so a quick gate costs no more than it did; every second after that.
  # It ends one of three ways: the gate ended (its status is in <name>.exit), its time ran out, or the
  # subshell it runs under was killed from outside before it could write a status.
  while [ ! -f "$ended" ]; do
    if ! ours "$pid"; then [ -f "$ended" ] || how=lost; break; fi
    elapsed=$((SECONDS - started))
    if [ "$limit" -gt 0 ] && [ "$elapsed" -ge "$limit" ]; then how=timeout; break; fi
    if [ "$beat" -gt 0 ] && [ "$elapsed" -ge "$next_beat" ]; then
      echo "   ... $name still running ($((elapsed / 60)) min), last line: $(last_line "$log")"
      next_beat=$((next_beat + beat))
    fi
    if [ "$polls" -lt 20 ]; then sleep 0.1 2>/dev/null || sleep 1; else sleep 1; fi
    polls=$((polls + 1))
  done 2>/dev/null   # bash 3.2 announces a killed subshell on stderr, whole; the verdict below says it
  case "$how" in
    ended) wait "$pid" || code=$? ;;
    timeout)
      { kill_gate "$pid"; wait "$pid"; } 2>/dev/null || true   # bash 3.2 announces the kill on stderr
      # It may have ended on its own in the very moment its time ran out: then it ended, with its status.
      if [ -f "$ended" ]; then
        how=ended code="$(cat "$ended")"
        case "$code" in ''|*[!0-9]*) code=1 ;; esac
      fi ;;
    lost)
      # Its group outlives the subshell while the gate is in it, so the group's id is still the gate's.
      wait "$pid" 2>/dev/null || code=$?
      kill -KILL -- "-$pid" 2>/dev/null || true ;;
  esac
  gate_pid=
  cat "$log"
  if [ "$how" = timeout ]; then
    # No Windows probes here: they run the gate again, with no limit, and it has just shown it hangs.
    echo "   ^ gate $name timed out after $(in_words "$limit") and was killed, with all it started; the last it said:"
    if [ -s "$log" ]; then
      tail -n 5 "$log" | LC_ALL=C tr '\r' '\n' | sed -n '/[^[:space:]]/p' | tail -n 5 | LC_ALL=C cut -c 1-200 | sed 's/^/   | /'
    else echo "   | (nothing at all)"; fi
    return 1
  fi
  if [ "$how" = lost ]; then
    echo "   ^ gate $name ended without a status: what it ran under was killed from outside (exit $code, $((SECONDS - started)) s)"
    return 1
  fi
  if [ "$code" -eq 0 ]; then
    echo "   ok ($((SECONDS - started)) s)"; return 0
  fi
  echo "   ^ gate $name exited $code ($((SECONDS - started)) s)"
  # The probes only for what they are for, a gate that never started: 127, or not a word printed. They
  # run it four times more with no limit, and a gate that ran and said why it failed needs none of that.
  if $windows && { [ "$code" -eq 127 ] || [ ! -s "$log" ]; }; then windows_probes "$bin"; fi
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
