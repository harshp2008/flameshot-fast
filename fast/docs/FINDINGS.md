# Findings

Legend: **[M]** measured by me in this project with the scripts in `fast/`;
**[R]** reported by Harsh, not re-measured; **[I]** inferred, not measured.

## 1. The problem (from the task description) [R]

A whole-desktop screenshot through `org.freedesktop.portal.Screenshot` is slow
because GNOME writes the entire stage as one PNG at the largest monitor scale
(4320x1080 logical -> 10800x2700 px, 29 MP), single threaded.

| Setup | Time [R] |
|---|---|
| `flameshot full`, displays on | 9 to 25 s |
| `flameshot full`, laptop screen only | 0.8 s |
| `flameshot gui` window, displays on / laptop only | ~5.6 s / 0.8 s |
| laptop 2.1 MP / + phone (1x) 4.6 MP / + tablet (1x) 7.2 MP / all 1x 10.9 MP | 0.8 / 2.9 / 4.4 / 6.8 s (about 0.65 s per MP) |
| portal request alone (non-interactive) | ~3.6 s, client adds ~2 s (Flameshot) to ~6 s (gnome-screenshot) |
| `flameshot screen -n 0` | 5.7 s (captures the stage, then crops) |
| window screenshot through the portal | 0.77 s |
| ScreenCast portal, standalone Python/GStreamer, remembered permission | Create+Select+Start 0.01 s, first raw frame 0.11 to 0.20 s, PNG encode of it ~0.85 s (not needed here) |

## 2. Baseline: stock Flameshot v15.0.0 [M]

* v15.0.0 configures and builds **unchanged** against Qt 6.4.2 / GCC 13.3 /
  Ninja (`fast/scripts/build.sh`); no compat patch, v14.0.0 fallback not needed.
* Harness: `fast/scripts/bench.sh`. For each run it starts a fresh daemon from
  an isolated `XDG_CONFIG_HOME`, then times the CLI call until the first
  Flameshot window with a real size exists (`spawn-at query windows`, read
  only, polled back to back; one query takes ~85 ms, so resolution is ~0.1 s),
  or for `full` until the command exits. Only the PIDs it started are killed.
  `gui` = `flameshot gui` (with three monitors the first window is the
  monitor selector, shown after the grab); `edit0` = `flameshot screen -n 0 -e`
  (capture window of Qt screen 0, no selector); `full` = `flameshot full -p DIR`.
* Layout during the runs: laptop 1920x1080@0,0 scale 1, a 1464x915 (scale
  1.7486) and a 936x432 (scale 2.5) virtual display, 4320 px wide in total.
  Note the two virtual displays came back in the **opposite order** compared
  with the task description (tablet at x=1920, phone at x=3384): the UScreen
  displays are re-enumerated, so screen indices and positions are not stable
  between sessions. The patched code matches by geometry, not by index.

Final stock baseline (5 runs each, seconds):

| Mode | ok | min | median | max | raw |
|---|---|---|---|---|---|
| `gui` | 5/5 | 7.262 | 8.293 | 13.632 | 8.293 7.979 7.262 10.747 13.632 |
| `edit0` | 4/5 | 6.404 | 6.700 | 15.207 | 6.404 6.461 6.940 15.207 (+1 failed) |
| `full` | 4/5 | 10.719 | 10.840 | 13.042 | 10.719 13.042 10.891 10.788 (+1 failed) |

Failures are the existing 15 s portal timeout in `freeDesktopPortal()` ("The
xdg-desktop-portal backend did not respond", then "Unable to capture screen");
a capture that takes longer than 15 s is simply lost. The 15.207 s `edit0`
value is a run that finished just inside that limit.

An earlier attempt with a looser window detector gave nonsense (edit0 min
0.619 s) because Flameshot maps a 1x1 helper window right away; that attempt is
kept in `fast/out/bench-stock-v1-small-window-bug.log` and is **not** used
(3/5 `gui`, 1/5 `full` succeeded in it; the machine was apparently slower
then). Variation between the two attempts (gui median 12.9 s vs 8.3 s) shows
how noisy this is; treat the baseline as "7 to 15 s, sometimes failing".

Sanity check of the patched build with the feature **off** (default), one run:
`gui` 7.857 s, selector window opens: no regression visible [M, n=1].

## 3. Patched build [M unless marked]

### Offline tests (`fast/scripts/test.sh`), all pass, repeated runs stable

Everything runs on a private D-Bus bus without service directories, a mock
portal (`fast/tests/mock_portal.py`), an offscreen Qt platform and a private
PipeWire daemon with a test video source (`fast/tests/pw_source.cpp`).
No access to the real portal, display, sound devices or WirePlumber.

| Area | Test | Result |
|---|---|---|
| stream -> monitor | exact match, the real 3-monitor layout, reordered streams, +/-1 px fractional rounding, wrong size/position, ambiguous, exact beats near, mirrored outputs never share a stream | pass (`tst_pure`, 13 cases incl. the buffer tests) |
| format conversion | BGRx, RGBx, BGRA, RGBA channel order and opaque alpha, stride padding, short buffer, bad stride, zero/huge size, null | pass |
| portal absent | `ScreenCast portal unavailable`, 1 ms | pass |
| portal v3 (no restore tokens) | refused, no session created | pass |
| user cancels dialog | response 1 -> `cancelled by the user`, session closed | pass |
| Start never answers | `Start timed out after 1500 ms`, session closed | pass |
| Start returns no streams / streams match no screen | clear error; the second also sets `forgetToken` | pass |
| OpenPipeWireRemote fails | error, session closed | pass |
| PipeWire connected but no frame | `No video frame within 1500 ms`, session closed | pass |
| SelectSources arguments | `types=1 multiple=true persist_mode=2 cursor_mode=1` + the stored token | pass |
| new token returned even when the capture failed | single-use tokens are not lost | pass |
| end to end through real PipeWire (private daemon) | frame arrives, pixel pattern intact, **57 ms** for the whole call incl. portal handshake with the mock | pass |
| DPR | 320 px frame for a 160 px logical screen -> DPR 2 | pass |
| subset | only the requested screen is read from a 2-stream portal | pass |
| whole `flameshot` binary (offscreen, mock portals) | `screen -n 0` saves the ScreenCast frame (`screencast-capture-ms=56..76`), Screenshot portal not called; token written to `flameshot.ini`; token passed to SelectSources next time; cancel and no-frame fall back to the Screenshot portal and still save a capture with a warning in the log; default config never calls ScreenCast; no portal at all -> failure logged and stock path attempted | pass |

Sessions are closed on every error path after `CreateSession` (checked through
the mock's `Close` log) and right after the frame copy on success.

### Build from a clean checkout [M]

A detached worktree of `screencast-capture` in a temp dir: `fast/scripts/build.sh`
succeeded in 1 min 37 s wall (Ninja, `-j$(nproc)`), `dist/bin/flameshot --version`
prints `v15.0.0`, and `fast/scripts/test.sh` passed against that fresh build.
(The configure step downloads KDSingleApplication and QtColorWidgets with
FetchContent, as upstream does, so it needs network access.)

### Real portal, without the dialog [M]

* `org.freedesktop.portal.ScreenCast`: `version` 5, `AvailableSourceTypes` 7,
  `AvailableCursorModes` 7 on this machine.
* `CreateSession` and `SelectSources(types=1, multiple, cursor_mode=1,
  persist_mode=2)` return response 0 on the real portal (probe script, then
  `Session.Close`). `Start` was **not** called, so no dialog appeared and
  nothing was shared.

### Live capture with the patched build [M, run by Harsh, 2026-11-11]

First live run: the Share dialog was answered once (first attempt failed with
`No ScreenCast stream matches screen 0 (1920x1080+0+0)`, so the token was
dropped and the fallback to the stock portal worked; cause not determined,
probably a monitor not ticked). The next attempt took 9.45 s (dialog), the one
after **230 ms for 3 screens with no dialog**: GNOME honoured the restore token
from a fresh daemon start (host app, isolated config).

`fast/scripts/bench.sh -b dist/bin/flameshot -l patched -n 5 -m gui,edit0 -c fast/out/cfg-patched`,
same harness and layout caveats as the baseline (seconds to first real-size window):

| Mode | ok | min | median | max | raw | capture call (ms, from log) |
|---|---|---|---|---|---|---|
| `gui` (3 screens grabbed) | 5/5 | 0.405 | 0.634 | 4.364 | 0.634 4.364 0.443 0.405 0.664 | 255 2104 184 177 294 |
| `edit0` (1 screen) | 5/5 | 0.531 | 0.783 | 1.865 | 1.865 0.824 0.783 0.729 0.531 | 589 281 243 239 161 |

Versus stock medians: `gui` 8.29 -> 0.63 s (about 13x), `edit0` 6.70 -> 0.78 s
(about 8.5x); no failed runs (stock failed 1 in 5 for `edit0`). Outliers
(`gui` 4.36 s with a 2.1 s capture call, `edit0` 1.87 s) are slow first frames
or a cold start; cause not investigated. n=5, so treat medians as indicative.
Still not checked live: that the picture is visually correct and current, the
sharing indicator, and the monitor-selection UX.

Numbers that exist for the fast path:

* [R] standalone script: handshake 0.01 s, first frame 0.11 to 0.20 s.
* [M] our client code against a local PipeWire producer: 56 to 76 ms for the
  complete `grab()` including the mock handshake and one memcpy of a 800x800 or
  320x200 frame. This says our client is not the bottleneck; it says nothing
  about how fast GNOME delivers the frame.
* [I] expected live `gui` time to the selector/capture window: well under 1 s
  (about 0.15 to 0.4 s plus Flameshot's own window setup, which is ~0.8 s
  total on the laptop-only layout [R]). Not verified.

## 4. What is not verified

* Image content live (visual check), the first-attempt stream mismatch, mutter's first frame (is it the current
  picture of an idle screen? does the first buffer carry data or is it empty?
  the code skips empty/corrupted buffers and waits up to 2.5 s), the laptop
  panel's stream, the streams of the fractionally scaled virtual displays
  (position/size as reported, DPR math), the buffer type mutter picks when only
  MemFd/MemPtr are offered, the sharing indicator disappearing at once.
* Whether a restore token obtained by `flameshot` (host application, no app id,
  launched from a terminal or the app grid) is honoured on the next start in
  this GNOME/portal version. The portal keys permissions by app id; for host
  apps it can differ by launch context. If it is not, the dialog returns every
  time and the feature is useless in this setup (the 5-minute back-off limits
  the damage).
* Pointer-based monitor choice on Wayland: not available from Qt; unchanged
  from stock (selector).
* Multi-monitor `gui` with three real streams (only single-screen offscreen
  was exercised in the binary test; three-stream reads are tested at the
  library level with a two-stream mock).
* X11, KDE, Wlroots portals: not tested.
