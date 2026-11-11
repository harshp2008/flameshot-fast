// SPDX-License-Identifier: GPL-3.0-or-later

#include "colornames.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtMath>

namespace {

struct Named
{
    const char* name;
    quint32 rgb;
};

// A modest table of everyday color names; the nearest one (in CIE Lab) wins.
const Named kNames[] = {
    { "Black", 0x000000 },       { "Charcoal", 0x36454f },
    { "Dark gray", 0x505050 },   { "Gray", 0x808080 },
    { "Silver", 0xc0c0c0 },      { "Light gray", 0xdcdcdc },
    { "White", 0xffffff },       { "Ivory", 0xfffff0 },
    { "Beige", 0xf5f5dc },       { "Cream", 0xfff8e7 },
    { "Sand", 0xe6d5b8 },        { "Tan", 0xd2b48c },
    { "Taupe", 0x8b7d6b },       { "Khaki", 0xc3b091 },
    { "Brown", 0x8b5a2b },       { "Dark brown", 0x4a2c16 },
    { "Chocolate", 0xd2691e },   { "Rust", 0xb7410e },
    { "Terracotta", 0xe2725b },  { "Red", 0xe02020 },
    { "Dark red", 0x8b0000 },    { "Crimson", 0xdc143c },
    { "Maroon", 0x800000 },      { "Burgundy", 0x800020 },
    { "Wine", 0x722f37 },        { "Rose", 0xeed5d2 },
    { "Pink", 0xffc0cb },        { "Hot pink", 0xff69b4 },
    { "Salmon", 0xfa8072 },      { "Coral", 0xff7f50 },
    { "Peach", 0xffdab9 },       { "Orange", 0xff8c00 },
    { "Amber", 0xffbf00 },       { "Gold", 0xffd700 },
    { "Yellow", 0xffeb3b },      { "Light yellow", 0xffffe0 },
    { "Olive", 0x808000 },       { "Olive green", 0x6b8e23 },
    { "Lime", 0x32cd32 },        { "Green", 0x2e9e44 },
    { "Forest green", 0x228b22 }, { "Dark green", 0x0b5d1e },
    { "Sage", 0x9cb687 },        { "Mint", 0xb5ead7 },
    { "Teal", 0x008080 },        { "Turquoise", 0x40e0d0 },
    { "Cyan", 0x00bcd4 },        { "Sky blue", 0x9fc5d4 },
    { "Light blue", 0xadd8e6 },  { "Steel blue", 0x4682b4 },
    { "Blue gray", 0x6c7a89 },   { "Slate gray", 0x708090 },
    { "Blue", 0x1e66d0 },        { "Royal blue", 0x4169e1 },
    { "Navy", 0x000080 },        { "Midnight blue", 0x191970 },
    { "Indigo", 0x4b0082 },      { "Slate blue", 0x6a5acd },
    { "Purple", 0x800080 },      { "Violet", 0x8a2be2 },
    { "Lavender", 0xe6e6fa },    { "Lilac", 0xc8a2c8 },
    { "Orchid", 0xda70d6 },      { "Magenta", 0xff00ff },
};

struct Lab
{
    double l, a, b;
};

double lin(double c)
{
    return c <= 0.04045 ? c / 12.92 : qPow((c + 0.055) / 1.055, 2.4);
}

Lab toLab(const QColor& c)
{
    const double r = lin(c.redF()), g = lin(c.greenF()), b = lin(c.blueF());
    double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    double y = (0.2126 * r + 0.7152 * g + 0.0722 * b);
    double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) {
        return t > 0.008856 ? qPow(t, 1.0 / 3.0) : 7.787 * t + 16.0 / 116.0;
    };
    x = f(x);
    y = f(y);
    z = f(z);
    return { 116 * y - 16, 500 * (x - y), 200 * (y - z) };
}

} // namespace

QString ColorNames::nameFor(const QColor& color)
{
    const Lab target = toLab(color);
    double best = 1e18;
    const char* bestName = "Black";
    for (const Named& n : kNames) {
        const Lab l = toLab(QColor::fromRgb(n.rgb));
        const double d = qPow(l.l - target.l, 2) + qPow(l.a - target.a, 2) +
                         qPow(l.b - target.b, 2);
        if (d < best) {
            best = d;
            bestName = n.name;
        }
    }
    return QString::fromLatin1(bestName);
}

QString ColorNames::hex(const QColor& c)
{
    return c.name(QColor::HexRgb);
}

QString ColorNames::rgb(const QColor& c)
{
    return QStringLiteral("rgb(%1, %2, %3)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue());
}

QString ColorNames::hsl(const QColor& c)
{
    float h, s, l;
    c.getHslF(&h, &s, &l);
    return QStringLiteral("hsl(%1, %2%, %3%)")
      .arg(h < 0 ? 0 : qRound(h * 360))
      .arg(qRound(s * 100))
      .arg(qRound(l * 100));
}

QString ColorNames::hsv(const QColor& c)
{
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    return QStringLiteral("hsv(%1, %2%, %3%)")
      .arg(h < 0 ? 0 : qRound(h * 360))
      .arg(qRound(s * 100))
      .arg(qRound(v * 100));
}

QString ColorNames::format(const QColor& c, const QString& name)
{
    const QString n = name.toLower();
    if (n == QLatin1String("rgb")) {
        return rgb(c);
    }
    if (n == QLatin1String("hsl")) {
        return hsl(c);
    }
    if (n == QLatin1String("hsv")) {
        return hsv(c);
    }
    return hex(c);
}

QList<QColor> ColorNames::shades(const QColor& color, int count)
{
    // Same hue and saturation, lightness from light to dark around `color`
    float h, s, l;
    color.getHslF(&h, &s, &l);
    QList<QColor> out;
    const int mid = count / 2;
    for (int i = 0; i < count; ++i) {
        const float step = (mid - i) * 0.12f;
        out << QColor::fromHslF(h < 0 ? 0 : h, s, qBound(0.04f, l + step, 0.96f));
    }
    out[mid] = color;
    return out;
}

ColorNames::History::History(const QString& path)
  : m_path(path)
{
    if (m_path.isEmpty()) {
        const QString dir =
          QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_path = dir + QStringLiteral("/color-history.json");
    }
    load();
}

void ColorNames::History::load()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    for (const auto& v : doc.array()) {
        QColor c(v.toString());
        if (c.isValid() && !m_colors.contains(c)) {
            m_colors << c;
        }
    }
    while (m_colors.size() > maxEntries) {
        m_colors.removeLast();
    }
}

void ColorNames::History::save() const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonArray arr;
    for (const QColor& c : m_colors) {
        arr << hex(c);
    }
    QSaveFile f(m_path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void ColorNames::History::add(const QColor& color)
{
    m_colors.removeAll(color);
    m_colors.prepend(color);
    while (m_colors.size() > maxEntries) {
        m_colors.removeLast();
    }
    save();
}

void ColorNames::History::remove(const QColor& color)
{
    m_colors.removeAll(color);
    save();
}
