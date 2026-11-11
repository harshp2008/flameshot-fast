# Design: ScreenCast portal capture backend

Base: Flameshot v15.0.0 (`f4f2e4e8`), built unchanged against Qt 6.4.2.

## 1. How v15.0.0 captures today (read from the code)

| Question | Answer | Where |
|---|---|---|
| Where is the full-desktop pixmap requested? | `ScreenGrabber::grabEntireDesktop()` -> `unixScreenshot()` -> `freeDesktopPortal()` (Screenshot portal, `interactive=false`, answer is a PNG path that is loaded with `QPixmap(path)` and deleted). | `src/utils/screengrabber.cpp:54-186, 189-229, 303-347` |
| Who calls it? | `CaptureWidget` constructor (`flameshot gui`, `screen -e`), `Flameshot::screen()` (`screen`), `Flameshot::full()` -> `grabFullDesktop()` (`full`). | `capturewidget.cpp:109-127`, `flameshot.cpp:220-270` |
| How does a screen map into the pixmap? | `cropToMonitor()` computes the bounding box of all `QScreen::geometry()`, derives scale = `pixmap.width() / totalLogicalWidth` and crops `geometry * scale`; if scale differs from the screen DPR it rescales; finally sets the DPR to the screen's. | `screengrabber.cpp:642-780` |
| Where is the monitor chosen? | After the grab. Single screen: index 0. `screen -n N -e` / `req.selectedMonitor()`: `preSelectedMonitor`. `captureActiveMonitor` (not on Wayland, it errors out). Otherwise the interactive monitor selector (`createMonitorPreviews`, thumbnails come from `cropToMonitor` of the full pixmap; its initial highlight is the screen from `QGuiAppCurrentScreen`). | `selectMonitorAndCrop()`, `createMonitorPreviews()` |
| What does the window do with it? | `CaptureWidget` moves/resizes to the chosen screen's logical geometry and paints the cropped pixmap whose DPR is the screen DPR. | `capturewidget.cpp:127-225` |

Consequence: the whole stage is rendered by GNOME as one PNG at the largest
scale (10800x2700 for 4320x1080 logical), then cropped to one monitor. The
time is spent in the compositor/portal, which Flameshot cannot influence.

## 2. Prior art on `upstream/*` branches and why none helps directly

| Branch | What it is | Verdict |
|---|---|---|
| `gnome_adapter` (2023-06, jack9603301, one commit) | Opt-in CMake flag `USE_WAYLAND_GNOME`; shells out to `gnome-screenshot -f /tmp/gnome_flameshot_png` and loads the PNG. Written for the era when Flameshot called the (now access-denied) `org.gnome.Shell.Screenshot` directly. | Never merged. It adds an external process and the same PNG round trip (the thing that is slow), hard-codes a world-writable `/tmp` filename and has fall-through `case` bugs. **Reused idea:** make the alternative backend a compile-time option with everything `#ifdef`ed out, and keep it opt-in. Nothing else reusable. |
| `fix_wayland_multiscreen` (2022-04) | Two small KDE-Wayland fixes: resize the widget to the pixmap in `paintEvent`, bounds-check the screen number. | Superseded by the v15 single-screen capture window and `cropToMonitor`. **Lesson:** per-screen window geometry must come from `QScreen::geometry()`, which v15 already does. |
| `monitor` (2022-10) | WIP: log `screenAdded`/`screenRemoved`. | Nothing to reuse. |
| `screen_bug` (2026-04) | `main.cpp`: `flameshot screen -n N` (non-`--edit`) must forward the screen number via `CaptureRequest(SCREEN_MODE, delay, screenNumber)`. Already part of the v15 line (`main.cpp:592`). | Confirms that `grabScreen(screen)` -> `grabEntireDesktop(ok, index)` is the single-screen entry point, which is the seam used here. |

Also relevant from `git log`: the `grim` adapter was removed in v14 in favour of
the portal, and a maintainer states that portal slowness "needs to be
adjusted with the portal implementation, not flameshot". Hence the backend is
optional and OFF by default.

## 3. Design

### 3.1 Seam (fewest changed lines)

All the choosing logic already lives in `ScreenGrabber`, and everything
downstream of `unixScreenshot()` only needs a pixmap per screen. So:

* New, self-contained `src/utils/screencastgrabber.{h,cpp}`: one class
  `ScreenCastGrabber`, plus two pure helpers (`matchStreamsToScreens`,
  `imageFromBuffer`) so they can be unit tested without a session.
* `ScreenGrabber` gets one private member, `QHash<int, QPixmap> m_screenCastShots`
  (screen index -> pixmap) and one private method `tryScreenCast(int preSelected)`.
* In `grabEntireDesktop()` (Unix branch) the call `unixScreenshot(ok)` becomes
  "if `tryScreenCast()` succeeded use an empty full pixmap, else `unixScreenshot()`".
* `cropToMonitor()` starts with: "if a ScreenCast image exists for this monitor
  return it". The selector, `captureActiveMonitor`, `screen -n N` and
  single-monitor paths then work unchanged (they all end in `cropToMonitor`).
* `grabFullDesktop()` (`flameshot full`) is deliberately **not** changed: it
  needs the whole stage as one image and the portal path is the right tool.

### 3.2 Which screens are grabbed

* Pre-selected monitor, single screen, or `captureActiveMonitor`: only that
  screen (one stream). On Wayland the `captureActiveMonitor` branch errors out
  in v15; unchanged.
* Interactive monitor selector (the v15 default on multi-monitor): the choice is
  made *after* the grab, so all screens are grabbed from one session (their
  streams are read in parallel, ~one frame time total) and the thumbnails come
  straight from them. Pointer position cannot be read from Qt on Wayland, so
  nothing new is assumed; the selector keeps highlighting
  `QGuiAppCurrentScreen` as before.

### 3.3 Portal side (Qt DBus, same style as `freeDesktopPortal`)

`CreateSession` -> `SelectSources(types=1, multiple=true, cursor_mode,
persist_mode=2, restore_token)` -> `Start` -> `OpenPipeWireRemote` (fd). Every
step that returns a request handle listens for `Response` *before* the call
(pre-computed request path from `handle_token`), reusing the generated
`OrgFreedesktopPortalRequestInterface`. The `streams` array is demarshalled to
`{node, position, size}`. `restore_token` from Start is returned to the caller
who stores it. The session is closed (`org.freedesktop.portal.Session.Close`)
by an RAII guard on every path, immediately after the frames are copied, so the
GNOME sharing indicator disappears at once.

### 3.4 PipeWire side (libpipewire, no GStreamer)

A private `pw_loop` + `pw_context` + `pw_core` (via `pw_context_connect_fd` on the
portal fd) are driven from the Qt GUI thread with a `QSocketNotifier` on the loop
fd, so there is no extra thread and no locking. One `pw_stream` per wanted
node, `EnumFormat` = {BGRx, BGRA, RGBx, RGBA} with any size, and after format
negotiation a `Buffers` param with `dataType = MemFd | MemPtr` only (no
DMA-BUF). The first buffer with `chunk->size > 0`, no `CORRUPTED` flag and a
mapped pointer is copied into a `QImage` (this is the only copy). Streams are
destroyed right after. A `QEventLoop` + `QTimer` bounds the wait.

### 3.5 Mapping stream -> screen, and DPR

Start reports each stream's `position` and `size` in logical coordinates. A
stream matches the `QScreen` whose `geometry()` has the same top-left and
size (tolerance 2 px for fractional-scale rounding); the match must be unique
and one-to-one or the whole attempt fails. The frame is physical-size, so
`DPR = frameWidth / logicalWidth` is set on the image (no resampling).

### 3.6 Fail-safe

`ScreenCastGrabber::grab()` returns an error string instead of throwing; any
failure (no portal, no ScreenCast interface, no PipeWire, cancelled dialog,
timeout, unexpected format, unmatched screen) logs one warning and
`ScreenGrabber` continues with the unmodified `unixScreenshot()`. After a
failure the backend is skipped for 5 minutes in that process so a daemon is not
slowed by repeated timeouts or repeated dialogs.

### 3.7 Config and build

* Keys (Unix only, not in the settings dialog): `useScreenCastCapture`
  (bool, default **false**), `screenCastEmbedCursor` (bool, default false),
  `screenCastRestoreToken` (string, written by the backend only).
* CMake: `option(USE_SCREENCAST ... ON)`, `pkg_check_modules(PIPEWIRE libpipewire-0.3)`;
  when missing or non-Unix the feature is `#ifdef`ed out and the key is still
  accepted (so a shared config does not become invalid).

### 3.8 Rebase cost

Touched upstream files: `screengrabber.{h,cpp}` (about 110 added lines, most of
them the self-contained `tryScreenCast()`),
`confighandler.{h,cpp}` (3 option lines + getters), `CMakeLists.txt` /
`src/CMakeLists.txt` / `src/utils/CMakeLists.txt` (a few lines). Everything else
is new files or under `fast/`.
