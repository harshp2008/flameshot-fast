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
    MOCK_SHOT_SRC="${MOCK_SHOT_SRC:-}" MOCK_MODE="$1" MOCK_PW="$2" MOCK_STREAMS="${3:-40:0,0,800,600}" MOCK_SHOT="${MOCK_SHOT:-}" \
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
    SRC1=$!
    PIDS+=("$SRC1")
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

echo "== flameshot binary (offscreen Qt, mock portals, private PipeWire) =="
FS="${FLAMESHOT_BIN:-$ROOT/build/src/flameshot}"
if [ -x "$FS" ] && [ -n "${NODE:-}" ]; then
    kill "$SRC1" 2>/dev/null; wait "$SRC1" 2>/dev/null # sources link to every consumer
    "$B/pw_source" 800 800 >"$SB/source2.out" 2>"$SB/source2.err" &
    PIDS+=($!)
    for _ in $(seq 100); do grep -q NODE_ID "$SB/source2.out" && break; sleep 0.05; done
    NODE2="$(sed -n 's/^NODE_ID=//p' "$SB/source2.out")"
    export XDG_SESSION_TYPE=wayland MOCK_SHOT="$SB/portal-shot.png"
    export XDG_CONFIG_HOME="$SB/fs-cfg"
    export XDG_DATA_HOME="$SB/fs-data"; mkdir -p "$XDG_DATA_HOME"
    INI="$XDG_CONFIG_HOME/flameshot/flameshot.ini"

    fs_capture() { # name -> runs `screen -n 0`, result in $SB/out/shot.png, log in $SB/fs.log
        rm -rf "$SB/out"; mkdir -p "$SB/out"
        timeout 40 "$FS" screen -n 0 -p "$SB/out" >"$SB/fs.log" 2>&1
        FSRC=$?
        FSPNG="$(ls "$SB"/out/*.png 2>/dev/null | head -1)"
    }
    fs_size() { python3 -c "
import struct,sys
d=open(sys.argv[1],'rb').read(); print(struct.unpack('>II', d[16:24])[0], struct.unpack('>II', d[20:28])[1] if False else struct.unpack('>I', d[20:24])[0])" "$1"; }
    new_cfg() { # use_screencast
        rm -rf "$XDG_CONFIG_HOME"; mkdir -p "$XDG_CONFIG_HOME/flameshot"
        printf '[General]\ndisabledTrayIcon=true\nshowStartupLaunchMessage=false\nuseScreenCastCapture=%s\nallowMultipleGuiInstances=true\n' "$1" >"$INI"
    }

    # A: enabled and working -> the ScreenCast image is what gets saved
    # A0: no saved permission, normal capture: no dialog, ScreenCast not even tried
    new_cfg true; start_mock ok real "$NODE2:0,0,800,800"
    fs_capture
    if ! grep -q 'CreateSession' "$MOCK_LOG" && grep -q '^Screenshot' "$MOCK_LOG" && grep -q "FLAMESHOT_SCREENCAST_SETUP=1" "$SB/fs.log"; then
        pass "no saved permission: dialog is never opened in a normal capture, hint logged, stock path used"
    else
        fail "no-token normal capture"; cat "$MOCK_LOG"; fi
    # A: explicit setup run (FLAMESHOT_SCREENCAST_SETUP=1) -> ScreenCast image is what gets saved
    new_cfg true; start_mock ok real "$NODE2:0,0,800,800"
    export FLAMESHOT_SCREENCAST_SETUP=1
    MOCK_PW_SOCKET="$PIPEWIRE_RUNTIME_DIR/pipewire-0" fs_capture
    unset FLAMESHOT_SCREENCAST_SETUP
    if [ -n "$FSPNG" ] && [ "$(fs_size "$FSPNG")" = "800 800" ] && grep -q "screencast-capture-ms=" "$SB/fs.log" && ! grep -q '^Screenshot' "$MOCK_LOG"; then
        pass "enabled: saved image is the 800x800 ScreenCast frame, Screenshot portal not used  [$(grep -o 'screencast-capture-ms=[0-9]* screens=[0-9]*' "$SB/fs.log")]"
    else
        fail "enabled + working (rc=$FSRC png=$FSPNG size=$(fs_size "$FSPNG"))"; sed 's/^/      /' "$SB/fs.log" | head; cat "$MOCK_LOG" | sed 's/^/      mock: /'
    fi
    grep -q '^screenCastRestoreToken=.*|tok-new' "$INI" && pass "restore token stored in Flameshot's own config" || { fail "token stored"; cat "$INI"; }

    # B: second capture sends the stored token back to the portal
    MOCK_PW_SOCKET="$PIPEWIRE_RUNTIME_DIR/pipewire-0" fs_capture
    grep -q 'SelectSources restore_token=tok-new' "$MOCK_LOG" && pass "stored token is passed to SelectSources on the next capture" || { fail "token reuse"; cat "$MOCK_LOG"; }

    # B2: a token saved by another launcher is neither sent nor used, no dialog
    start_mock ok real "$NODE2:0,0,800,800"
    sed -i 's/^screenCastRestoreToken=.*/screenCastRestoreToken=some-other-launcher|zzz/' "$INI"
    fs_capture
    if ! grep -q 'CreateSession' "$MOCK_LOG" && grep -q "no saved permission for this launcher" "$SB/fs.log"; then
        pass "token from another launcher/layout is ignored: no portal call, hint logged, stock path used"
    else
        fail "foreign-launcher token"; cat "$MOCK_LOG"; head "$SB/fs.log"; fi

    # B3: launcher mode "auto": dialog only when this layout has no token
    new_cfg true; start_mock ok real "$NODE2:0,0,800,800"
    FLAMESHOT_SCREENCAST_SETUP=auto MOCK_PW_SOCKET="$PIPEWIRE_RUNTIME_DIR/pipewire-0" fs_capture
    grep -q 'SelectSources restore_token= ' "$MOCK_LOG" && grep -q 'screencast-capture-ms' "$SB/fs.log" && grep -q '^screenCastRestoreToken=.*|.*|tok-new' "$INI" \
        && pass "auto mode without a token: asks (setup), stores a per-layout token" || { fail "auto mode setup"; cat "$MOCK_LOG"; cat "$INI"; }
    start_mock ok real "$NODE2:0,0,800,800"
    FLAMESHOT_SCREENCAST_SETUP=auto MOCK_PW_SOCKET="$PIPEWIRE_RUNTIME_DIR/pipewire-0" fs_capture
    grep -q 'SelectSources restore_token=tok-new' "$MOCK_LOG" && pass "auto mode with a token: normal silent capture, token sent" || { fail "auto mode with token"; cat "$MOCK_LOG"; }
    # a layout change (different monitor set) = different key: token not used
    sed -i 's/\(screenCastRestoreToken=[^|]*|\)[^|]*/\1other-layout:1x1/' "$INI"
    start_mock ok real "$NODE2:0,0,800,800"
    fs_capture
    ! grep -q 'CreateSession' "$MOCK_LOG" && grep -q "no saved permission" "$SB/fs.log" && pass "different monitor layout: saved token not used, no dialog in a normal capture" || fail "layout change"

    # C: user cancels the dialog -> Screenshot portal result is used
    new_cfg true; start_mock cancel silent
    FLAMESHOT_SCREENCAST_SETUP=1 fs_capture
    if [ -n "$FSPNG" ] && grep -q '^Screenshot' "$MOCK_LOG" && grep -q "ScreenCast capture failed, using the screenshot portal" "$SB/fs.log"; then
        pass "cancel: warning logged, stock Screenshot portal used, a capture was still saved"
    else
        fail "cancel fallback (rc=$FSRC png=$FSPNG)"; sed 's/^/      /' "$SB/fs.log" | head; fi

    # D: PipeWire gives no frame -> fallback too (waits the 2.5 s frame timeout)
    new_cfg true; start_mock ok silent "$NODE2:0,0,800,800"
    FLAMESHOT_SCREENCAST_SETUP=1 fs_capture
    if [ -n "$FSPNG" ] && grep -q '^Screenshot' "$MOCK_LOG" && grep -q "No video frame" "$SB/fs.log"; then
        pass "no PipeWire frame: warning logged and stock path used"
    else
        fail "frame timeout fallback (rc=$FSRC)"; sed 's/^/      /' "$SB/fs.log" | head; fi

    # E: portal without ScreenCast at all (only Screenshot) is covered by the
    # absent case above; here: feature off (default) must not touch ScreenCast
    new_cfg false; start_mock ok real "$NODE2:0,0,800,800"
    fs_capture
    if [ -n "$FSPNG" ] && grep -q '^Screenshot' "$MOCK_LOG" && ! grep -q 'CreateSession' "$MOCK_LOG"; then
        pass "default (useScreenCastCapture=false): ScreenCast portal is never called"
    else
        fail "disabled by default (rc=$FSRC)"; cat "$MOCK_LOG"; fi

    # F: no portal at all and feature on -> still a capture attempt on the stock path
    stop_mock; new_cfg true
    FLAMESHOT_SCREENCAST_SETUP=1 fs_capture
    grep -q "ScreenCast capture failed" "$SB/fs.log" && pass "no portal service: ScreenCast failure logged, stock path attempted (fails here because no Screenshot portal either: rc=$FSRC)" || { fail "no portal"; head "$SB/fs.log"; }

    # G: `flameshot ocr` copies recognized text (needs tesseract), line breaks kept
    if command -v tesseract >/dev/null; then
        "$B/make_text_png" "$SB/text.png" 2>/dev/null
        MOCK_SHOT_SRC="$SB/text.png" start_mock ok silent
        new_cfg false
        rm -rf "$SB/out"; mkdir -p "$SB/out"
        timeout 60 "$FS" ocr --accept-on-select -c -p "$SB/out" --region 800x800+0+0 >"$SB/fs.log" 2>&1
        if grep -q "OCR text copied to clipboard (50 characters)" "$SB/fs.log"; then
            pass "flameshot ocr: text recognized, line and paragraph breaks kept (50 chars)  [$(grep -o 'OCR text copied[^)]*)' "$SB/fs.log")]"
        else
            fail "flameshot ocr (rc=$?)"; cat "$SB/fs.log" "$SB/mock.out" "$MOCK_LOG" | head -20; sed 's/^/      /' "$SB/fs.log" | head; mkdir -p "$ROOT/fast/out"; cp "$SB"/out/*.png "$ROOT/fast/out/ocr-debug.png" 2>/dev/null
        fi
    else
        echo "SKIP  tesseract not installed"
    fi

    # H: `flameshot color` (stand-alone color picker), scripted: no person, widgets rendered to PNG
    if python3 -c "import PIL" 2>/dev/null; then
        python3 - "$SB/colors.png" <<'PY'
import sys
from PIL import Image, ImageDraw
im = Image.new("RGB", (800, 800), (168, 199, 210))
ImageDraw.Draw(im).rectangle([380, 380, 420, 420], fill=(203, 182, 172))
im.save(sys.argv[1])
PY
        MOCK_SHOT="$SB/portal.png" MOCK_SHOT_SRC="$SB/colors.png" start_mock ok silent
        new_cfg false
        rm -rf "$SB/pick"
        FLAMESHOT_COLORPICKER_DUMP="$SB/pick" FLAMESHOT_COLORPICKER_AT=400,400 timeout 60 "$FS" color >"$SB/fs.log" 2>&1
        if grep -q "picked #cbb6ac" "$SB/fs.log" && grep -q "hsl=hsl(19, 23%, 74%)" "$SB/fs.log" && [ -s "$SB/pick/overlay.png" ] && [ -s "$SB/pick/window.png" ]; then
            pass "flameshot color: pixel picked from the frozen screen, all four formats shown, overlay and window rendered  [$(grep -o 'hex=.*' "$SB/fs.log")]"
        else
            fail "flameshot color"; head "$SB/fs.log"
        fi
        if find "$XDG_DATA_HOME" -name color-history.json -exec grep -q cbb6ac {} + 2>/dev/null; then
            pass "flameshot color: picked color stored in the history file (sandboxed data dir)"
        else
            fail "color history file"
        fi
    else
        echo "SKIP  flameshot color (python3 PIL missing)"
    fi
else
    echo "SKIP  flameshot binary ($FS) not built or PipeWire e2e unavailable"
fi

echo
[ "$FAIL" -eq 0 ] && echo "ALL TESTS PASSED" || echo "$FAIL TEST(S) FAILED"
exit "$FAIL"
