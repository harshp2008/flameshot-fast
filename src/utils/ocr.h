// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QPixmap>
#include <QString>

/**
 * Text recognition through the `tesseract` command line tool (not linked, so
 * Flameshot only needs it when the `ocr` subcommand is used).
 */
namespace Ocr {

/// Cleans raw tesseract output: drops form feeds and surrounding blank lines,
/// keeps line and paragraph breaks.
QString tidy(const QString& raw);

/// Runs tesseract on the image. Returns the text (possibly empty); `error` is
/// set when tesseract is missing or fails.
QString recognize(const QPixmap& image, QString& error);

/// Puts text on the clipboard through `wl-copy` (Wayland), which keeps a
/// background process alive as clipboard owner. A Flameshot process that sets
/// the clipboard itself loses it when it exits on GNOME Wayland. Returns false
/// when wl-copy is unavailable (caller falls back to the Qt clipboard).
bool copyTextPersistent(const QString& text);

/// recognize() plus a desktop notification showing what was (not) found.
/// Returns the text to copy, empty when nothing should be copied.
QString recognizeWithNotification(const QPixmap& image);

}
