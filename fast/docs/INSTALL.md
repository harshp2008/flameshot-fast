# Installing flameshot-fast

## 0. One line (Ubuntu 24.04, amd64)

```bash
curl -fsSL https://raw.githubusercontent.com/harshp2008/flameshot-fast/screencast-capture/fast/packaging/install.sh | bash
```

Downloads the newest GitHub release, verifies its SHA-256, installs the `.deb` with
`sudo apt` and runs `flameshot-fast-setup` for you (config, shortcuts, spawn-at).
Remove again with `... | bash -s -- --uninstall`. Read the script first if you like:
[fast/packaging/install.sh](../packaging/install.sh).

## A. Debian/Ubuntu package by hand

Built on Ubuntu 24.04 (amd64, Qt 6.4). Needs GNOME on Wayland for the fast path.

```bash
sudo apt install ./flameshot-fast_15.0.0+fast1_amd64.deb    # pulls the dependencies
flameshot-fast-setup                                         # as YOUR user, once
```

The package **replaces** the distribution's `flameshot` (Conflicts/Replaces/Provides).
`apt install` also installs the recommended `tesseract-ocr`, `tesseract-ocr-eng`
and `wl-clipboard` (OCR and clipboard persistence).

`flameshot-fast-setup` (per user, no root):

* adds `useScreenCastCapture`, `showDesktopNotification`, `screenCastPointerCommand`
  to `~/.config/flameshot/flameshot.ini` (original saved as `flameshot.ini.before-flameshot-fast`)
* asks (when run in a terminal) which color the toolbar should use: your desktop theme's accent
  color (read from the GTK theme, for example `Yaru-sage`), Flameshot's original purple,
  classic blue, or a hex color you type; Enter leaves it unchanged. Skip the question with
  `--theme theme|default|blue|keep|#rrggbb`.
* sets three GNOME shortcuts (an existing custom shortcut on the same key is updated):

  | Key | Action |
  |---|---|
  | Super+Shift+S | capture image |
  | Super+Shift+T | capture, **copy recognized text** (OCR) |
  | Super+Print | capture image |

* installs `spawn-at` (bundled copy, also a GNOME extension) if it is not on the machine,
  so the monitor under the pointer is captured without the selector. Log out and in once
  for its extension. Without it the monitor selector is shown.

First capture only: GNOME shows a **Share** dialog. Tick **all** screens, keep
"Remember this selection", press Share. After that captures are silent. Unplugging or
plugging a monitor needs one more Share (once per monitor layout).

Undo the per-user changes: `flameshot-fast-setup --undo`. Remove the package: `sudo apt remove flameshot-fast`
(then `sudo apt install flameshot` for the stock one).

## B. From source (this repository)

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-svg-dev qt6-tools-dev \
     qt6-tools-dev-tools qt6-l10n-tools libpipewire-0.3-dev libspa-0.2-dev git \
     tesseract-ocr tesseract-ocr-eng wl-clipboard
git clone https://github.com/harshp2008/flameshot-fast && cd flameshot-fast
git checkout screencast-capture
fast/scripts/build.sh                    # ./dist, nothing system-wide
fast/scripts/test.sh                     # offline tests
fast/packaging/build-deb.sh              # builds the .deb into fast/out/dist/
fast/scripts/install.sh --shortcuts      # user-level install into ~/.local/opt/flameshot-fast
```

## C. Troubleshooting

* `~/.cache/flameshot-fast/history.log` has the output of the last runs of the shortcut launcher.
* No Share dialog but slow capture: run `FLAMESHOT_SCREENCAST_SETUP=1 flameshot gui` from the launcher you use.
* Not on GNOME/Wayland: the new paths are skipped and Flameshot behaves like upstream.
