# Decisions

Each entry: decision, alternatives, why. Chosen conservatively because nobody
was available to ask.

1. **Layout: the project folder is the fork clone itself.** Alternatives: an
   inner `flameshot/` clone gitignored by an outer repo. Why: the task update
   said so; it is a drop-in fork, rebases are plain git, and extra files sit in
   `fast/` so they cannot collide with upstream paths.

2. **Base tag v15.0.0, no Qt compat patch needed.** The stock v15.0.0 configures
   and builds unchanged against Qt 6.4.2 / GCC 13 (checked with
   `fast/scripts/build.sh`), so the v14.0.0 fallback was not used.

3. **Backend is opt-in (`useScreenCastCapture`, default false).** Alternative:
   default on. Why: a first use needs a human click, a maintainer stated that
   portal slowness belongs in the portal, and a silent behaviour change in an
   upstream-bound patch is unacceptable.

4. **Wayland only.** The new path is tried only when `DesktopInfo::waylandDetected()`
   is true and `useX11LegacyScreenshot` is off. X11 already has a fast native grab.

5. **Seam: `ScreenGrabber::cropToMonitor()` plus one call in `grabEntireDesktop()`.**
   Alternatives: a new `grabScreen` overload used by every caller; replacing
   `unixScreenshot()`. Why: the monitor selector, `screen -n`, single-monitor and
   edit modes all end in `cropToMonitor`, so ~40 changed lines cover them all
   (see DESIGN.md). `flameshot full` is deliberately left alone.

6. **When the monitor is not known yet (selector), grab all monitors in one
   session.** Alternative: ask for the monitor first, which would change the v15
   UX. Why: conservative, keeps behaviour identical; costs only parallel frame
   reads.

7. **PipeWire driven from the Qt thread via `QSocketNotifier` on the `pw_loop`
   fd.** Alternatives: worker thread with `pw_thread_loop`; GStreamer. Why: no
   locking, no extra dependency (the task excluded GStreamer), trivially
   bounded by a timer. Verified against a real (private) PipeWire daemon.

8. **Config keys, not a settings-dialog entry.** Three Unix-only options,
   registered in `confighandler.cpp` so a shared config stays valid. Why: the
   settings dialog has translated strings and layout code that would add
   upstream churn; the feature is experimental.

9. **Restore token lives in Flameshot's `flameshot.ini` (`screenCastRestoreToken`).**
   Always saved immediately after Start (tokens are single use), even when the
   capture then fails. Dropped when the permission does not cover every
   monitor, so the dialog can fix it.

10. **Failure cool-down of 5 minutes per process.** Alternative: retry every
    capture. Why: a daemon would otherwise pay a timeout or show the dialog on
    every hotkey press after a cancel. 5 minutes is a guess, not a measurement. Exception: when the saved
    permission lacks a monitor (token dropped, dialog due) there is no back-off,
    so the next capture offers the dialog and the grant can be fixed at once.

11. **Timeouts.** 3 s per portal step, 2.5 s for the first frame, 60 s for Start
    when no token exists (a human has to click), all in `Options`.

12. **Frames are not rescaled.** DPR = frame width / logical width. The stock
    path rescales the cropped PNG to the Qt DPR; we keep native pixels.

13. **Did not trigger the GNOME Share dialog.** The task said the first request
    needs a human and the owner was away. No restore token existed, so no live
    ScreenCast benchmark was run. Only a no-dialog probe (CreateSession +
    SelectSources + Close on the real portal) was made. See TODO_FOR_HARSH.md.

14. **Offline tests use a private bus without service directories** (so the real
    xdg-desktop-portal cannot be bus-activated), a private PipeWire daemon
    (`PIPEWIRE_RUNTIME_DIR` in a temp dir) with a test source that links
    itself (no WirePlumber, which could grab the real audio devices), and the
    `offscreen` Qt platform.

15. **clang-format-18** (`/usr/bin/clang-format-18`) was used; the `clang-format` on
    PATH is v23 and would format differently. Unmodified upstream files are
    clean under 18, so the diff only contains intended changes.

16. **Benchmark window detector ignores tiny windows.** Flameshot maps a 1x1
    helper window early (parent for the portal call); the first baseline
    attempt counted it and gave nonsense (0.6 s). Kept as
    `fast/out/bench-stock-v1-small-window-bug.log` for the record, redone.

17. **`bench.sh` restarts the daemon for every run** (cold, consistent for stock
    and patched) and kills only the PIDs it started; it refuses to run if any
    `flameshot` process or D-Bus name already exists.

18. **No permission dialog during normal captures; explicit setup only
    (`FLAMESHOT_SCREENCAST_SETUP=1 flameshot gui`).** After a first install on
    the real desktop the dialog appeared on every shortcut press (nothing
    ticked, and a no-back-off rule re-prompted each time) and the fallback
    capture lagged the laptop. Whether GNOME ties the token to the launching
    context ("bash" appeared as the requester) is NOT established; the cause
    seen is a missing grant plus the re-prompt rule.
