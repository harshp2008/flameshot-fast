#!/usr/bin/env bash
# Benchmark harness: time from launching a Flameshot capture command until the
# first Flameshot window exists (or, for `full`, until the command exits).
#
#   fast/scripts/bench.sh -b BIN -l LABEL [-n RUNS] [-m MODES] [-c CFGDIR]
#                         [-s key=value]...
#
#   -b BIN     flameshot binary (default: dist/bin/flameshot)
#   -l LABEL   label used in the output table (default: basename of BIN dir)
#   -n RUNS    runs per mode (default 5)
#   -m MODES   comma list of: gui, edit0, edit1, edit2 (= `screen -n N -e`),
#              full (= `full -p DIR`). Default: gui,edit0,full
#   -c CFGDIR  use this XDG_CONFIG_HOME (kept afterwards, so a ScreenCast
#              restore token survives). Default: a fresh temp dir per script run.
#   -s k=v     extra [General] setting written to the isolated flameshot.ini
#
# Needs: spawn-at (read-only `query windows`), gdbus, bc.
# Only processes started by this script are ever killed. Refuses to run when a
# Flameshot daemon is already running, because CLI calls would be routed to it.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$ROOT/dist/bin/flameshot"
LABEL=""
RUNS=5
MODES="gui,edit0,full"
CFGDIR=""
SETTINGS=()
OUT="$ROOT/fast/out"

while getopts "b:l:n:m:c:s:" o; do
    case "$o" in
        b) BIN="$OPTARG" ;;
        l) LABEL="$OPTARG" ;;
        n) RUNS="$OPTARG" ;;
        m) MODES="$OPTARG" ;;
        c) CFGDIR="$OPTARG" ;;
        s) SETTINGS+=("$OPTARG") ;;
        *) sed -n '2,20p' "$0"; exit 2 ;;
    esac
done
[ -n "$LABEL" ] || LABEL="$(basename "$(dirname "$BIN")")"
[ -x "$BIN" ] || { echo "no such binary: $BIN" >&2; exit 2; }
mkdir -p "$OUT"

SB="$(mktemp -d /tmp/w11c-sandbox.XXXXXX)"
PIDS=()
cleanup() {
    local p
    for p in "${PIDS[@]:-}"; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done
    wait 2>/dev/null
    if [[ "$SB" == /tmp/w11c-sandbox.* ]] && [ -d "$SB" ]; then
        findmnt -T "$SB" >/dev/null 2>&1 # record that it is not a mount point
        if ! findmnt -M "$SB" >/dev/null 2>&1; then
            rm -rf "$SB"
        else
            echo "refusing to rm -rf mount point $SB" >&2
        fi
    fi
}
trap cleanup EXIT

if [ -z "$CFGDIR" ]; then
    CFGDIR="$SB/cfg"
fi
mkdir -p "$CFGDIR/flameshot"
INI="$CFGDIR/flameshot/flameshot.ini"
if [ ! -f "$INI" ]; then
    printf '[General]\ndisabledTrayIcon=true\nshowStartupLaunchMessage=false\n' >"$INI"
fi
for kv in "${SETTINGS[@]:-}"; do
    [ -n "$kv" ] || continue
    k="${kv%%=*}"
    if grep -q "^$k=" "$INI"; then
        sed -i "s|^$k=.*|$kv|" "$INI"
    else
        sed -i "/^\[General\]/a $kv" "$INI"
    fi
done
export XDG_CONFIG_HOME="$CFGDIR"

name_owned() {
    gdbus call --session --dest org.freedesktop.DBus \
        --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner org.flameshot.Flameshot 2>/dev/null |
        grep -q true
}
fs_windows() { spawn-at query windows 2>/dev/null | awk 'NR>1 && tolower($3) ~ /flameshot/'; }

if pgrep -x flameshot >/dev/null || name_owned; then
    echo "a Flameshot is already running; stop it first (not touching it)." >&2
    exit 3
fi

now() { date +%s.%N; }
median() { sort -n | awk '{a[NR]=$1} END{ if(NR%2){print a[(NR+1)/2]} else {printf "%.3f\n",(a[NR/2]+a[NR/2+1])/2} }'; }

start_daemon() {
    "$BIN" >"$SB/daemon.log" 2>&1 &
    DPID=$!
    PIDS+=("$DPID")
    local i
    for i in $(seq 200); do
        name_owned && return 0
        sleep 0.05
    done
    echo "daemon did not register on D-Bus" >&2
    return 1
}
stop_daemon() {
    kill "$DPID" 2>/dev/null
    wait "$DPID" 2>/dev/null
}

run_once() { # mode -> sets RESULT to seconds, or FAIL (no subshell: PIDs must stay tracked)
    local mode="$1" args t0 t1 i gpid shots
    case "$mode" in
        gui) args=(gui) ;;
        edit[0-9]) args=(screen -n "${mode#edit}" -e) ;;
        full) shots="$SB/shots"; rm -rf "$shots"; mkdir -p "$shots"; args=(full -p "$shots") ;;
        *) RESULT=FAIL; return ;;
    esac
    start_daemon || { RESULT=FAIL; return; }
    : >"$SB/cli.log"
    t0=$(now)
    "$BIN" "${args[@]}" >"$SB/cli.log" 2>&1 &
    gpid=$!
    PIDS+=("$gpid")
    if [ "$mode" = full ]; then
        for i in $(seq 1200); do
            kill -0 "$gpid" 2>/dev/null || break
            sleep 0.02
        done
        t1=$(now)
        wait "$gpid" 2>/dev/null
        ls "$shots"/*.png >/dev/null 2>&1 || { stop_daemon; RESULT=FAIL; return; }
    else
        local w=""
        for i in $(seq 1200); do
            w=$(fs_windows)
            [ -n "$w" ] && break
            kill -0 "$gpid" 2>/dev/null || break
        done
        t1=$(now)
        if [ -z "$w" ]; then kill "$gpid" 2>/dev/null; stop_daemon; RESULT=FAIL; return; fi
    fi
    kill "$gpid" 2>/dev/null
    wait "$gpid" 2>/dev/null
    stop_daemon
    # capture call's own time, if the build logs it
    grep -h -o 'screencast-capture-ms=[0-9.]*' "$SB/cli.log" "$SB/daemon.log" 2>/dev/null |
        tail -1 >>"$SB/capture-ms.txt"
    RESULT="$(echo "$t1 - $t0" | bc -l)"
}

echo "label=$LABEL bin=$BIN runs=$RUNS cfg=$CFGDIR"
printf '%-12s %-7s %5s %9s %9s %9s  %s\n' label mode ok min median max raw
IFS=',' read -ra MODE_LIST <<<"$MODES"
for mode in "${MODE_LIST[@]}"; do
    vals=()
    fails=0
    : >"$SB/capture-ms.txt"
    for r in $(seq "$RUNS"); do
        run_once "$mode"
        v=$RESULT
        if [ "$v" = FAIL ]; then
            fails=$((fails + 1))
            echo "  run $r of $mode FAILED; log:" >&2
            sed 's/^/    /' "$SB/cli.log" "$SB/daemon.log" | head -8 >&2
        else
            vals+=("$(printf '%.3f' "$v")")
        fi
        # leftover Flameshot windows would skew the next run
        for _ in $(seq 40); do [ -z "$(fs_windows)" ] && break; sleep 0.1; done
        [ -n "$(fs_windows)" ] && { echo "leftover Flameshot window after run; stopping" >&2; exit 4; }
    done
    if [ "${#vals[@]}" -gt 0 ]; then
        mn=$(printf '%s\n' "${vals[@]}" | sort -n | head -1)
        mx=$(printf '%s\n' "${vals[@]}" | sort -n | tail -1)
        md=$(printf '%s\n' "${vals[@]}" | median)
        printf '%-12s %-7s %2d/%-2d %9s %9s %9s  %s\n' "$LABEL" "$mode" "${#vals[@]}" "$RUNS" \
            "$mn" "$md" "$mx" "${vals[*]}"
        printf '%s\n' "${vals[@]}" >"$OUT/bench-$LABEL-$mode.txt"
    else
        printf '%-12s %-7s %2d/%-2d %9s\n' "$LABEL" "$mode" 0 "$RUNS" "all runs failed"
    fi
    if [ -s "$SB/capture-ms.txt" ]; then
        echo "  capture call (from log): $(tr '\n' ' ' <"$SB/capture-ms.txt")"
    fi
done
