# Final report and TODO for Harsh

## Report

**What exists.** `screencast-capture` = Flameshot v15.0.0 + 15 small commits (4 of them are the code, the rest tests, scripts and docs)
(`git log --oneline v15.0.0..screencast-capture`). It adds an optional,
off-by-default backend (`useScreenCastCapture=true`) that grabs the needed
monitor(s) through the ScreenCast portal + libpipewire as in-memory images and
falls back to the stock Screenshot portal path on any failure. Code: new
`src/utils/screencastgrabber.{h,cpp}`, about 110 added lines in
`screengrabber.{h,cpp}`, 3 config keys, a CMake option (`USE_SCREENCAST`, needs
`libpipewire-0.3`, compiled out otherwise). Everything else is under `fast/`.

**Verified (and how).**
* Stock v15.0.0 builds unchanged on Qt 6.4.2; the patched tree builds from a
  clean worktree in 1 m 37 s (`fast/scripts/build.sh`).
* `fast/scripts/test.sh`, all green, repeated 5 times: pure logic (monitor <->
  stream matching on your real layout, format conversion), every fallback path
  forced against a mock portal on a private bus (portal absent, portal v3,
  user cancel, Start timeout, no streams, unmatched streams, OpenPipeWireRemote
  error, no frame), session closed on every path, token handling, a real
  frame through a **real private PipeWire daemon** (57 ms for the whole
  `grab()`), DPR math, and the full `flameshot` binary on the offscreen
  platform: ScreenCast image saved, token stored in `flameshot.ini`, reused,
  cancel/no-frame/no-portal fall back with a logged warning, default config
  never touches ScreenCast.
* Real portal on this machine accepts our `CreateSession`/`SelectSources`
  (`version` 5), probed without calling `Start` (no dialog).
* Stock baseline with `bench.sh`, 5 runs each: `gui` 7.3 / 8.3 / 13.6 s
  (min/median/max, 5 of 5 ok); `edit0` 6.4 / 6.7 / 15.2 s (4 of 5, one hit the
  15 s portal timeout); `full` 10.7 / 10.8 / 13.0 s (4 of 5). Patched build with
  the feature off: `gui` 7.9 s (1 run), no regression visible.

**NOT verified.**
* **Any live ScreenCast capture and therefore any patched benchmark number.**
  No restore token existed and I did not open the Share dialog while you were
  away. The 0.15 s figure for the patched `gui` is your standalone-script
  measurement plus my inference; mine is only "our client takes 56 to 76 ms
  against a local PipeWire producer".
* Whether mutter's first buffer is a valid, current picture on an idle screen;
  the laptop panel's own stream; real stream `position`/`size` for the
  fractional-scale displays; sharing indicator vanishing; whether GNOME
  honours a restore token from a host (non-sandboxed) application across
  restarts of Flameshot. If it does not, you get the dialog every time and the
  feature is not worth using (5-minute back-off limits the annoyance).
* The monitor under the pointer on Wayland: Qt cannot tell, so `gui` keeps the
  stock behaviour (monitor selector, now filled from the fast frames).
* KDE/wlroots/X11 sessions.

**What you must do.**
1. First live run (needs you; see below), then the live benchmark.
2. No `sudo` and no extra package were needed; nothing was installed
   system-wide. (`clang-format` on PATH is v23; I used `/usr/bin/clang-format-18`
   because upstream CI uses clang-format-lint-action v0.18 = 18.)
3. Publishing is in `PUBLISHING.md` (not run).

**Chance upstream accepts it.** Low: I would say 10 to 20 %, and lower for
merging as-is. Reasons: a maintainer's stated stance that portal slowness
"needs to be adjusted with the portal implementation, not flameshot"; the
`grim`/`gnome-screenshot` adapters were dropped or never merged, so they prefer
one backend; about 1000 new lines is not "small"; it needs libpipewire and a
human click on first use; and it has no live numbers yet. What helps: it is
opt-in, isolated, has real fallback tests, and the problem is real and
measurable. Best path: live numbers first, then a GitHub Discussion, then an
RFC (`fast/docs/UPSTREAM_RFC_DRAFT.md`). Keeping it as your fork is the
realistic outcome.

## TODO 1: first live run (you must click Share once)

Uses an isolated config, never `~/.config/flameshot`, and never touches an
already running Flameshot (stop yours first if it is running: `flameshot gui`
CLI calls would be routed to it, and `bench.sh` refuses to run then).

```bash
cd "/home/harsh/Documents/GITHUB PROJECTS/flameshot-fast"
fast/scripts/build.sh                      # only if dist/ is stale
export CFG="$PWD/fast/out/cfg-patched"
mkdir -p "$CFG/flameshot"
printf '[General]\ndisabledTrayIcon=true\nshowStartupLaunchMessage=false\nuseScreenCastCapture=true\n' > "$CFG/flameshot/flameshot.ini"
XDG_CONFIG_HOME="$CFG" dist/bin/flameshot &        # daemon, remember its PID: kill %1 afterwards
sleep 1
XDG_CONFIG_HOME="$CFG" dist/bin/flameshot gui
```

Expect: a GNOME dialog to share your screen with Flameshot. Tick **all three
monitors** (laptop panel, phone, tablet) and press **Share**. Then the monitor
selector appears. stderr should show `screencast-capture-ms=NNN screens=3`.
If it says `ScreenCast capture failed ...`, copy that line into this file:
it names the stage that failed. Check the token was stored:

```bash
grep screenCastRestoreToken "$CFG/flameshot/flameshot.ini"
```

Press Esc in the selector, then `kill %1` (only your daemon from this shell).
Run `XDG_CONFIG_HOME="$CFG" dist/bin/flameshot &` and `flameshot gui` again:
**no dialog** should appear this time. If it does, GNOME is not honouring
the token for this launch context; stop here and note it in FINDINGS.

## TODO 2: live benchmark (after TODO 1 stored the token)

```bash
fast/scripts/bench.sh -b dist/bin/flameshot -l patched -n 5 -m gui,edit0,edit1,edit2 -c "$CFG"
```

It restarts the daemon each run, kills only its own PIDs, and prints
min/median/max plus the `screencast-capture-ms` values from the log. Paste the
table into `fast/docs/FINDINGS.md` (section 3, "Live capture") and mark it
measured. Baseline for comparison is in section 2 (same harness, same layout
caveat: UScreen re-orders the virtual displays, so `edit1`/`edit2` may hit a
different physical display than last time).

## TODO 3: housekeeping

* If a monitor was left out of the Share dialog the log says `No ScreenCast
  stream matches screen N ... will be asked again next time`; the next capture
  shows the dialog again, tick all three.
* To forget the permission: delete the `screenCastRestoreToken=` line in the
  isolated ini, or revoke it in GNOME Settings -> Privacy -> Screen Sharing.
* Publish: `PUBLISHING.md`.
