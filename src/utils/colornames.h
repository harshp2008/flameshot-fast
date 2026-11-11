// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QString>

/// Helpers of the stand-alone color picker (`flameshot color`).
namespace ColorNames {

/// Friendly name of the nearest known color ("Sky blue", "Rose", ...).
QString nameFor(const QColor& color);

/// "#rrggbb"
QString hex(const QColor& color);
/// "rgb(203, 182, 172)"
QString rgb(const QColor& color);
/// "hsl(19, 23%, 74%)"
QString hsl(const QColor& color);
/// "hsv(19, 15%, 80%)"
QString hsv(const QColor& color);
/// One of "hex", "rgb", "hsl", "hsv" (anything else gives hex)
QString format(const QColor& color, const QString& formatName);

/// Lighter and darker variants of `color` (lightest first), including
/// `color` itself at index `count / 2`.
QList<QColor> shades(const QColor& color, int count = 6);

/// Recently picked colors, newest first, kept in the user's data directory.
class History
{
public:
    /// `path` empty = the default file. Tests pass their own.
    explicit History(const QString& path = QString());
    QList<QColor> colors() const { return m_colors; }
    /// Moves/adds the color to the front; keeps at most `maxEntries`.
    void add(const QColor& color);
    void remove(const QColor& color);

private:
    void load();
    void save() const;
    QString m_path;
    QList<QColor> m_colors;
    static constexpr int maxEntries = 24;
};

} // namespace ColorNames
