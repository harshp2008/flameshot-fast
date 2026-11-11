# flameshot-fast

Flameshot v15.0.0 plus one optional capture backend: grab **one monitor** through
the **ScreenCast portal + PipeWire** as an in-memory image, instead of asking the
Screenshot portal for a PNG of the whole desktop.

Everything specific to this fork lives under `fast/` or in the small set of
files listed in [docs/DESIGN.md](docs/DESIGN.md) (section 3.8). The branch is
`screencast-capture`; `master` is a clean mirror of upstream.

## Why

On GNOME/Wayland the Screenshot portal renders the **whole stage** as one PNG at
the largest monitor scale, single threaded. With a laptop panel plus two virtual
displays (4320x1080 logical, 10800x2700 px) that is 29 MP and takes 9 to 25 s;
see [docs/FINDINGS.md](docs/FINDINGS.md) for the numbers. The ScreenCast portal
hands out per-monitor frames; a remembered permission makes the handshake about
10 ms and the first frame about 0.1 to 0.2 s (measured with a standalone
script, see FINDINGS for what is and is not verified here).

## Use

1. Build (installs into `./dist`, nothing system wide):

   ```bash
   fast/scripts/build.sh
   ```

   Needs the usual Flameshot build deps plus `libpipewire-0.3-dev` (without it
   the backend is compiled out: `-DUSE_SCREENCAST=OFF` does the same on purpose).

2. Run the offline tests (no display, no portal, no sudo; private D-Bus bus and
   private PipeWire daemon in a temp dir):

   ```bash
   fast/scripts/test.sh
   ```

3. Turn the backend on. It is **off by default**. In `flameshot.ini` (section
   `[General]`, normally `~/.config/flameshot/flameshot.ini`):

   ```ini
   useScreenCastCapture=true
   screenCastEmbedCursor=false   ; optional, draw the pointer into the capture
   ```

   `screenCastRestoreToken` is written by Flameshot itself; do not edit it.
   To try it without touching your real config, point `XDG_CONFIG_HOME` at a
   temp directory (see [../TODO_FOR_HARSH.md](../TODO_FOR_HARSH.md) for the exact
   first-run steps).

4. One-time setup (needs you): a normal capture **never** opens the permission
   dialog; without a saved permission it logs a hint and uses the stock path.
   Grant it explicitly, from the same launcher you will normally use:

   ```bash
   FLAMESHOT_SCREENCAST_SETUP=1 flameshot gui
   ```

   In the GNOME dialog click **every** monitor so they are all selected (Share
   stays greyed until something is selected), keep "Remember this selection",
   press Share. Flameshot stores the returned restore token in its own config;
   later captures are silent. If you cancel, a warning is logged and the normal
   Screenshot portal is used (no re-prompt for 5 minutes). If a monitor is
   missing from the grant or the layout changes, the token is dropped and the
   fast path stays off until you run the setup command again.

5. Benchmark (stock vs patched, same harness):

   ```bash
   fast/scripts/bench.sh -b fast/out/stock/flameshot -l stock -n 5 -m gui,edit0,full
   fast/scripts/bench.sh -b dist/bin/flameshot -l patched -n 5 -m gui,edit0 \
       -c fast/out/cfg-patched -s useScreenCastCapture=true
   ```

## What is covered

| Command | Behaviour |
|---|---|
| `flameshot gui` (several monitors) | all monitors are grabbed from one portal session (parallel streams), then the usual monitor selector is shown from those images |
| `flameshot screen -n N -e`, single-monitor desktops | only that monitor is grabbed |
| `flameshot screen` (no `-e`), `gui` after selection | same code path as above |
| `flameshot full` | **unchanged** (needs the whole stage as one image) |
| anything that fails | warning on stderr + log, then the unmodified Screenshot portal path |

Wayland sessions only. X11, macOS and Windows never touch the new code.

## Sync with upstream

```bash
fast/scripts/rebase-upstream.sh            # newest vX.Y.Z tag, fetches first
fast/scripts/rebase-upstream.sh v15.1.0 --no-fetch
```

The script rebases in a temp worktree and either creates a new local branch
`screencast-capture-on-<tag>` (clean) or lists the conflicting files and
changes nothing. Then rebuild and rerun `fast/scripts/test.sh`.
`master` is only ever fast-forwarded from `upstream/master`; never commit to it.

## Known limits

* A fresh daemon must be granted the Share dialog once (needs a human).
* The remembered permission must include every monitor (hold Shift/Ctrl in the
  dialog); if a monitor is missing or its geometry changed, the token is dropped
  and the dialog comes back on the next capture (no back-off). Single-monitor
  captures (`screen -n N -e`) keep working while their monitor is in the grant.
* The pointer cannot be read on Wayland from Qt; the monitor selector keeps
  highlighting Qt's idea of the current screen exactly as in stock v15
  (`captureActiveMonitor` still errors out on Wayland, unchanged).
* DMA-BUF buffers are deliberately not requested; compositors that only offer
  DMA-BUF would fall back to the stock path.
* Frames are used at the monitor's native resolution (device pixel ratio =
  frame width / logical width), so saved images can be a little larger than
  with the stock path on fractionally scaled monitors.
* No settings-dialog UI; config keys only.
