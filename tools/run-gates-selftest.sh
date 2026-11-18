#!/usr/bin/env bash
# THE SELF-TEST OF tools/run-gates.sh, on tiny script gates instead of the amps' — nothing to build.
#
# Usage: tools/run-gates-selftest.sh [run-gates.sh to test]      (the one next to this script unless said)
#
# Seven gates, in this order: one passes, one fails with a code of its own, one sleeps past the limit and
# says nothing, one prints (the last line redrawn with carriage returns) and then hangs, one has what it
# runs under killed from outside, one ends in the very moment its time runs out, one passes after all of
# them. With a limit of three seconds the run must show the two that ran out of time killed — and
# everything they started — with their output so far and heartbeats while they ran, the one that lost its
# runner red at once, the one that ended at the limit taken as ended, the failing one red with its code
# (and on Windows, having said why, no probes), the passing ones green, the order kept, and end with exit
# 1. A runner without the limit never ends on its own: the test gives it BOUND seconds, then kills it, and
# that is a red too.
set -euo pipefail

BOUND=60   # a green run takes ~10 s; Windows forks slowly and three gates time out there
here="$(cd "$(dirname "$0")" && pwd)"
script="${1:-$here/run-gates.sh}"
[ -f "$script" ] || { echo "run-gates-selftest.sh: no $script" >&2; exit 1; }

tmp="${TMPDIR:-/tmp}"
work="$(cd "$(mktemp -d "${tmp%/}/run-gates-selftest.XXXXXX")" && pwd)"
pids="$work/pids" out="$work/out.txt"
mkdir -p "$work/build/gates" "$work/bin" "$pids"

windows=false
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=true ;; esac

gate() {   # gate <name> <env or -> <body>: a gate as a shell script, listed the way CMake lists them
  printf '#!/bin/sh\n%s\n' "$3" > "$work/bin/$1"
  chmod +x "$work/bin/$1"
  printf '%s\t%s\t%s\n' "$1" "$work/bin/$1" "${2#-}" >> "$work/build/gates/Release.txt"
}
# Each hanging gate leaves its pid, and its child's, for the check that nothing is left. On Windows the
# hanging one becomes a native program, as a real gate is, so taskkill has a tree of its own to reach.
native=; if $windows; then native='exec ping -n 300 127.0.0.1 > /dev/null'; fi
# shellcheck disable=SC2016   # the gates' own scripts, expanded when they run
gate pass   GREETING=hi 'echo "pass: hello, $GREETING"; echo "pass: home $HOME"'
gate fail   - 'echo "fail: found 3 problems"; exit 3'
gate sleepy - "echo \$\$ > '$pids/sleepy'; sleep 300 & echo \$! > '$pids/sleepy-child'; wait"
gate hang   - "echo 'hang: step 1'; echo 'hang: step 2, waiting for a device'
printf 'hang: waiting, 1 of 3\\rhang: waiting, 3 of 3\\n'; echo \$\$ > '$pids/hang'
$native
sleep 300 & echo \$! > '$pids/hang-child'; wait"
gate lost   - "echo 'lost: losing what I run under'; echo \$\$ > '$pids/lost'
sleep 300 & echo \$! > '$pids/lost-child'; kill -KILL \$PPID; wait"
# Ending at the limit, made exact: told to stop, it writes the status as its runner would have, and ends.
# On Windows taskkill comes first and gives it no say, so there it simply times out.
gate late   - "trap 'echo 0 > \"$work/build/gates/late.exit\"; exit 0' TERM; echo 'late: working to the last moment'
echo \$\$ > '$pids/late'; sleep 300 & echo \$! > '$pids/late-child'; wait"
gate after  - 'echo "after: still here"'

# The run, in a process group of its own, so that a runner that never ends can be killed whole.
started=$SECONDS bounded=false rc=0
set -m
ORBITAMP_GATE_TIMEOUT_SEC=3 ORBITAMP_GATE_HEARTBEAT_SEC=1 bash "$script" "$work/build" Release > "$out" 2>&1 &
run=$!
set +m
while kill -0 "$run" 2>/dev/null; do
  if [ $((SECONDS - started)) -ge "$BOUND" ]; then
    bounded=true
    kill -TERM -- "-$run" 2>/dev/null || true; sleep 2; kill -KILL -- "-$run" 2>/dev/null || true
    break
  fi
  sleep 0.2 2>/dev/null || sleep 1
done
{ wait "$run"; } 2>/dev/null || rc=$?
took=$((SECONDS - started))

echo "---- the run ($took s, exit $rc) ----"
cat "$out"
echo "---- the checks ----"

alive() {   # anything the hanging gates started, still there
  local f
  for f in "$pids"/*; do
    if [ -f "$f" ] && kill -0 "$(cat "$f")" 2>/dev/null; then return 0; fi
  done
  if $windows && tasklist //FI "IMAGENAME eq PING.EXE" //NH 2>/dev/null | grep -qi 'ping\.exe'; then return 0; fi
  return 1
}
for _ in 1 2 3 4 5 6 7 8 9 10; do alive || break; sleep 0.3 2>/dev/null || sleep 1; done
left=false; if alive; then left=true; fi

block() {   # block <gate>: what the run printed for that gate
  awk -v head="-- gate: $1" '$0 == head { on = 1; print; next } /^-- gate: |^== gates/ { on = 0 } on' "$out"
}
both_there() { test -s "$pids/$1" && test -s "$pids/$2"; }
says() { block "$1" | grep -qF -- "$2"; }
says_line() { block "$1" | grep -qxF -- "$2"; }
silent_on() { ! says "$1" "$2"; }
no_probes() { ! $windows || silent_on fail "--- the file itself ---"; }
late_on_time() { if $windows; then says late "^ gate late timed out"; else says late "   ok (" && silent_on late "timed out"; fi; }
if $windows; then reds="5 of 7" named="fail sleepy hang lost late"; else reds="4 of 7" named="fail sleepy hang lost"; fi
order="$(sed -n 's/^-- gate: //p' "$out" | tr '\n' ' ')"

fails=0
check() {   # check <what> <command...>
  local what="$1"; shift
  if "$@"; then echo "  ok    $what"; else echo "  FAIL  $what"; fails=$((fails + 1)); fi
}
check "the run ended on its own, within $BOUND s"           test "$bounded" = false
check "exit code 1"                                         test "$rc" = 1
check "the gates in their order"                            test "$order" = "pass fail sleepy hang lost late after "
check "pass: its environment reached it"                    says pass "pass: hello, hi"
check "pass: a home of its own"                             says pass "pass: home $work/build/gates/home-pass."
check "pass: green"                                         says pass "   ok ("
check "fail: its output shown"                              says fail "fail: found 3 problems"
check "fail: red with its own code"                         says fail "^ gate fail exited 3"
check "sleepy: it started"                                  both_there sleepy sleepy-child
check "sleepy: heartbeats while it ran"                     says sleepy "... sleepy still running (0 min), last line: (nothing yet)"
check "sleepy: timed out"                                   says sleepy "^ gate sleepy timed out after 3 s and was killed"
check "sleepy: said to have said nothing"                   says sleepy "   | (nothing at all)"
check "hang: it started"                                    test -s "$pids/hang"
check "hang: its output so far shown"                       says hang "hang: step 2, waiting for a device"
check "hang: a heartbeat with its last line, newest frame"  says_line hang "   ... hang still running (0 min), last line: hang: waiting, 3 of 3"
check "hang: timed out, its last lines after the verdict"   says hang "   | hang: step 2, waiting for a device"
check "fail: having said why, no Windows probes"           no_probes
check "lost: it started"                                    both_there lost lost-child
check "lost: its output shown"                              says lost "lost: losing what I run under"
check "lost: red at once, ended without a status"           says lost "^ gate lost ended without a status"
check "late: ended at the limit, taken as ended"            late_on_time
check "after: ran, green"                                   says after "   ok ("
check "the summary: $reds red"                            grep -qF "== gates: $reds red" "$out"
check "the summary names them"                              grep -qE "red .* $named\$" "$out"
check "nothing the timed-out gates started is left"         test "$left" = false

# Whatever the runner left behind is killed here, so a red run of the test leaves nothing either.
for f in "$pids"/*; do [ -f "$f" ] && kill -KILL "$(cat "$f")" 2>/dev/null || true; done
if $windows && $left; then taskkill //F //IM PING.EXE > /dev/null 2>&1 || true; fi

if [ "$fails" -gt 0 ]; then
  echo "== run-gates self-test: $fails red (the run's files are in $work)"
  exit 1
fi
rm -rf "$work"
echo "== run-gates self-test: green"
