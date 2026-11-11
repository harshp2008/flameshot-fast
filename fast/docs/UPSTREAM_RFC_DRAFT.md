# RFC: optional ScreenCast-portal capture backend for multi-monitor Wayland

*Draft for docs/RFC.md. Not submitted. Numbers marked (measured) were taken on
the author's machine; (not measured) means it is an expectation, see
`fast/docs/FINDINGS.md` for the evidence.*

## Summary

Add an **optional, off-by-default** capture backend that grabs the monitor the
user is capturing through `org.freedesktop.portal.ScreenCast` + PipeWire as an
in-memory image, and falls back automatically to the existing Screenshot portal
path on any failure. About 1000 lines in two new files (clang-formatted, one third is
PipeWire boilerplate) plus about 140 added lines in `ScreenGrabber`, config and
CMake; nothing changes unless the user
sets `useScreenCastCapture=true`.

## Motivation

On GNOME/Wayland, `flameshot gui`, `screen` and `full` all call
`org.freedesktop.portal.Screenshot`. GNOME renders the **entire stage** into one
PNG at the largest monitor scale, single threaded, and Flameshot then crops one
monitor out of it. Cost is linear in pixels (measured, GNOME Shell 46,
xdg-desktop-portal-gnome):

| Layout (logical)            | PNG size   | portal + Flameshot |
|-----------------------------|-----------:|-------------------:|
| laptop panel only 1920x1080 | 2.1 MP     | 0.8 s |
| + 936x432 @2.5x display     | 4.6 MP (1x)| 2.9 s |
| + 1464x915 @1.75x display   | 7.2 MP (1x)| 4.4 s |
| 3 monitors, native scale    | 29 MP      | 9 to 25 s (stock gui: 7 to 14 s in the re-run, see FINDINGS) |

`flameshot screen -n 0` still takes ~5.7 s because it captures the whole stage
and crops afterwards, and the existing 15 s portal timeout makes `full` fail
outright on such setups (measured: 4 of 5 `flameshot full` runs failed with
"portal did not respond"). Multi-monitor and virtual-display setups (docking
stations, tablets as displays) are common; a screenshot tool that needs 10 s is
hard to use.

A maintainer previously said the slowness "needs to be adjusted with the
portal implementation, not flameshot". We agree it is a compositor/portal
problem and do not try to hide it. This RFC only adds a second, optional way to
ask for *one monitor*, using a portal that exists for exactly that purpose and
that the compositor can serve quickly (a window screenshot through the same
portal already takes 0.77 s; a ScreenCast first frame took 0.11 to 0.20 s,
measured with a standalone script).

## Detailed design

* `src/utils/screencastgrabber.{h,cpp}`: `ScreenCastGrabber::grab(screens,
  wanted, options)`.
  * `CreateSession`, `SelectSources(types=monitor, multiple=true,
    persist_mode=2, restore_token)`, `Start`, `OpenPipeWireRemote`, written in
    the same Qt DBus style as `freeDesktopPortal()`; `Response` listeners are
    installed before each call.
  * Streams are matched to `QScreen`s by the logical `position`/`size` the
    portal returns; ambiguous or missing matches fail the attempt.
  * PipeWire is used through libpipewire directly (no GStreamer), driven from
    the Qt thread with a `QSocketNotifier`. Only shared-memory buffers are
    requested; the first valid frame is copied into a `QImage` and the session
    is closed immediately, so the screen-sharing indicator does not linger.
* `ScreenGrabber::tryScreenCast()`: if enabled, on Wayland, and not in cool-down,
  grabs the needed screens (one if the monitor is already known, otherwise all,
  because the monitor selector runs after the grab), stores them per monitor,
  and `cropToMonitor()` returns them directly. On any failure it logs one
  warning and the unmodified `unixScreenshot()` runs. A failure disables the
  backend for 5 minutes in that process.
* Config (Unix only): `useScreenCastCapture` (default false),
  `screenCastEmbedCursor` (default false), `screenCastRestoreToken` (written by
  Flameshot; the portal's single-use restore token).
* CMake: `option(USE_SCREENCAST ... ON)` with `pkg_check_modules(libpipewire-0.3)`;
  without the library the code is `#ifdef`ed out. No other dependency.
* First use shows the compositor's one-time "Share" dialog; the user selects all
  monitors once. The returned restore token makes later captures dialog-free.

Prior art on `upstream/*` branches: `gnome_adapter` (2023) shelled out to
`gnome-screenshot` (same PNG round trip, hard-coded `/tmp` path, never merged);
its useful idea, a compile-time opt-in alternative backend, is kept here.

## Drawbacks

* A second capture path to maintain, and a new optional dependency (libpipewire).
* Needs one human interaction (the Share dialog) before it is fast.
* The screen-cast permission/indicator is a stronger capability than a
  screenshot from the user's point of view; hence opt-in and short-lived sessions.
* Compositors that offer only DMA-BUF streams fall back to the stock path.

## Alternatives

* Fix it in the portal/compositor (best, but not in Flameshot's hands; this RFC
  is a stop-gap and can be dropped when the portal gets fast).
* `org.gnome.Shell.Screenshot` D-Bus API: access denied for ordinary apps.
* Window screenshots through the portal: fast, but not a monitor capture.
* GStreamer `pipewiresrc`: heavier dependency than libpipewire.
* Making it the default: rejected until it has had real-world use.

## Unresolved questions

* Is an optional backend acceptable at all, or should this live in a fork?
* Should the setting appear in the settings dialog and the example config?
* Should `flameshot full` assemble the stage from per-monitor frames too?
* Preferred 5-minute failure back-off, or retry every time?
* Test strategy: this branch ships mock-portal tests with a private PipeWire
  daemon under `fast/tests/`; upstream's `tests/` are shell-based CLI checks.
