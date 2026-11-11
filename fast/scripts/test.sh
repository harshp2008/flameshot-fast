#!/usr/bin/env bash
# Builds and runs the offline tests in fast/tests. Needs no display, no portal
# and no real PipeWire session: everything runs on a private D-Bus bus (and a
# private PipeWire daemon for the end-to-end case) inside a temp directory.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$ROOT/fast/tests"

SB="$(mktemp -d /tmp/w11c-sandbox.XXXXXX)"
PIDS=()
cleanup() {
    local p
    for p in "${PIDS[@]:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done
    wait 2>/dev/null
    if [[ "$SB" == /tmp/w11c-sandbox.* ]] && [ -d "$SB" ]; then
        if findmnt -M "$SB" >/dev/null 2>&1; then
            echo "refusing to rm -rf mount point $SB" >&2
        else
            rm -rf "$SB"
        fi
    fi
}
trap cleanup EXIT

cmake -S "$T" -B "$ROOT/build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null &&
    cmake --build "$ROOT/build-tests" -j"$(nproc)" 2>&1 | tail -3
B="$ROOT/build-tests"

FAIL=0
pass() { printf 'PASS  %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; FAIL=$((FAIL + 1)); }

echo "== pure logic =="
if "$B/tst_pure" >"$SB/pure.log" 2>&1; then
    pass "tst_pure ($(grep -c '^PASS' "$SB/pure.log") cases)"
else
    fail "tst_pure"; cat "$SB/pure.log"
fi

# private session bus
export DBUS_SESSION_BUS_ADDRESS
DBUS_SESSION_BUS_ADDRESS="unix:path=$SB/bus"
# Minimal config without <servicedir>: nothing (e.g. the real
# xdg-desktop-portal) can be bus-activated on this private bus.
cat >"$SB/bus.conf" <<CONF
<busconfig>
  <type>session</type>
  <listen>$DBUS_SESSION_BUS_ADDRESS</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
CONF
dbus-daemon --config-file="$SB/bus.conf" --nofork --nopidfile &
PIDS+=($!)
for _ in $(seq 100); do [ -S "$SB/bus" ] && break; sleep 0.05; done
unset DISPLAY WAYLAND_DISPLAY
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="$SB/run"; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
export MOCK_LOG="$SB/mock.log"

MOCKPID=""
start_mock() { # mode pw [streams]
    stop_mock
    : >"$MOCK_LOG"
    MOCK_MODE="$1" MOCK_PW="$2" MOCK_STREAMS="${3:-40:0,0,800,600}" MOCK_SHOT="${MOCK_SHOT:-}" \
        python3 "$T/mock_portal.py" >"$SB/mock.out" 2>&1 &
    MOCKPID=$!
    PIDS+=("$MOCKPID")
    for _ in $(seq 100); do grep -q READY "$SB/mock.out" && return 0; sleep 0.05; done
    echo "mock portal did not start:"; cat "$SB/mock.out"; return 1
}
stop_mock() { [ -n "$MOCKPID" ] && { kill "$MOCKPID" 2>/dev/null; wait "$MOCKPID" 2>/dev/null; MOCKPID=""; }; }

# case NAME MODE PW EXPECT [need-close yes|no] [extra env...]
case_run() {
    local name="$1" mode="$2" pw="$3" expect="$4" needclose="$5"
    start_mock "$mode" "$pw" || { fail "$name (mock)"; return; }
    local out rc
    out="$("$B/flow_test" "$expect" 2>&1)"; rc=$?
    local closed=no; grep -q '^Close ' "$MOCK_LOG" && closed=yes
    if [ $rc -eq 0 ] && { [ "$needclose" = n/a ] || [ "$closed" = "$needclose" ]; }; then
        pass "$name  [$(echo "$out" | grep -E '^(elapsed_ms|error)=' | tr '\n' ' ')session closed: $closed]"
    else
        fail "$name (rc=$rc, session closed: $closed, want $needclose)"; echo "$out" | sed 's/^/      /'
    fi
}

echo "== fallback paths (mock portal, forced failures) =="
stop_mock
out="$("$B/flow_test" "ScreenCast portal unavailable" 2>&1)"; rc=$?
[ $rc -eq 0 ] && pass "portal absent  [$(echo "$out" | grep -E '^(elapsed_ms|error)=' | tr '\n' ' ')]" || { fail "portal absent"; echo "$out"; }
case_run "portal too old (v3, no tokens)" badver real "version 3" n/a
case_run "user cancels the dialog" cancel silent "cancelled by the user" yes
case_run "Start never answers (timeout)" hang silent "Start timed out" yes
case_run "Start gives no streams" nostream silent "no monitor streams" yes
case_run "streams match no screen (+ token forgotten)" mismatch silent "No ScreenCast stream matches" yes
case_run "OpenPipeWireRemote fails" ok error "OpenPipeWireRemote failed" yes
case_run "PipeWire never delivers a frame" ok silent "No video frame within" yes
start_mock mismatch silent; out="$("$B/flow_test" "No ScreenCast stream" 2>&1)"
echo "$out" | grep -q '^forget=1' && pass "mismatch sets forgetToken" || fail "mismatch sets forgetToken"
start_mock ok silent; FLOW_TOKEN=abc123 "$B/flow_test" "No video frame" >/dev/null 2>&1
grep -q 'restore_token=abc123 persist_mode=2 types=1 multiple=True cursor_mode=1' "$MOCK_LOG" &&
    pass "SelectSources gets types=1 multiple persist_mode=2, cursor hidden, saved restore_token" ||
    { fail "SelectSources arguments"; cat "$MOCK_LOG"; }
out="$(FLOW_TOKEN=abc123 "$B/flow_test" "No video frame" 2>&1)"
echo "$out" | grep -q '^token=tok-new' && pass "new restore token returned even though capture failed" || fail "token returned on failure"

echo "== end to end with a private PipeWire daemon =="
if command -v pipewire >/dev/null; then
    export PIPEWIRE_RUNTIME_DIR="$SB/pw"; mkdir -p "$PIPEWIRE_RUNTIME_DIR"; chmod 700 "$PIPEWIRE_RUNTIME_DIR"
    pipewire >"$SB/pipewire.log" 2>&1 &
    PIDS+=($!)
    for _ in $(seq 100); do [ -S "$PIPEWIRE_RUNTIME_DIR/pipewire-0" ] && break; sleep 0.05; done
    "$B/pw_source" >"$SB/source.out" 2>"$SB/source.err" &
    PIDS+=($!)
    for _ in $(seq 100); do grep -q NODE_ID "$SB/source.out" && break; sleep 0.05; done
    NODE="$(sed -n 's/^NODE_ID=//p' "$SB/source.out")"
    if [ -n "$NODE" ]; then
        export MOCK_PW_SOCKET="$PIPEWIRE_RUNTIME_DIR/pipewire-0"
        export FLOW_SCREENS="0,0,320,200" FLOW_EXPECT_PIXEL="0:5,7=66,7,5"
        start_mock ok real "$NODE:0,0,320,200"
        out="$("$B/flow_test" OK 2>&1)"; rc=$?
        if [ $rc -eq 0 ] && grep -q '^Close ' "$MOCK_LOG"; then
            pass "frame delivered through PipeWire, pixel pattern intact  [$(echo "$out" | grep -E '^(elapsed_ms|image|pixel)' | tr '\n' ' ')]"
        else
            fail "end to end (rc=$rc)"; echo "$out" | sed 's/^/      /'; sed 's/^/      src: /' "$SB/source.err" | tail -5; sed 's/^/      mock: /' "$SB/mock.out" | tail -8
        fi
        # logical 160x100 for a 320x200 frame -> DPR 2
        export FLOW_SCREENS="0,0,160,100" FLOW_EXPECT_DPR="0=2"
        start_mock ok real "$NODE:0,0,160,100"
        out="$("$B/flow_test" OK 2>&1)"; rc=$?
        [ $rc -eq 0 ] && pass "device pixel ratio = frame width / logical width  [$(echo "$out" | grep '^image' | tr '\n' ' ')]" ||
            { fail "dpr (rc=$rc)"; echo "$out" | sed 's/^/      /'; }
        # two screens, only the second wanted
        export FLOW_SCREENS="0,0,100,100;0,0,320,200" FLOW_WANT=1 FLOW_EXPECT_DPR="1=1"
        unset FLOW_EXPECT_PIXEL
        start_mock ok real "$NODE:0,0,320,200;99:0,0,100,100"
        out="$("$B/flow_test" OK 2>&1)"; rc=$?
        [ $rc -eq 0 ] && pass "only the wanted screen is read  [$(echo "$out" | grep '^image' | tr '\n' ' ')]" ||
            { fail "wanted subset (rc=$rc)"; echo "$out" | sed 's/^/      /'; }
        unset FLOW_SCREENS FLOW_WANT FLOW_EXPECT_DPR
    else
        fail "end to end: pw_source did not start"; cat "$SB/source.err" "$SB/pipewire.log" | head
    fi
else
    echo "SKIP  pipewire daemon not installed"
fi

echo
[ "$FAIL" -eq 0 ] && echo "ALL TESTS PASSED" || echo "$FAIL TEST(S) FAILED"
exit "$FAIL"
