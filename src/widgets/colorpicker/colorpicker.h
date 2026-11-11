// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * `flameshot color`: freezes the monitor under the pointer, lets the user pick
 * a pixel with a magnifier, copies the color (format from the config key
 * `colorPickerFormat`, default hex) and opens the Color Picker window with the
 * history and the HEX / RGB / HSL / HSV values. Runs its own event loop and
 * returns the process exit code. QApplication must exist.
 */
int runColorPicker();
