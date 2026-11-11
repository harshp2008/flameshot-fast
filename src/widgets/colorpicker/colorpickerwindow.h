// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/colornames.h"

#include <QColor>
#include <QList>
#include <QWidget>

class QLabel;
class QHBoxLayout;
class QTimer;
class QVBoxLayout;

/// A round color button of the history strip
class SwatchButton : public QWidget
{
    Q_OBJECT
public:
    SwatchButton(const QColor& color, QWidget* parent = nullptr);
    void setSelected(bool selected);
    QColor color() const { return m_color; }

signals:
    void clicked(const QColor& color);
    void removeRequested(const QColor& color);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    QColor m_color;
    bool m_selected = false;
};

/// A vertical bar of lighter and darker variants of the current color
class ShadeBar : public QWidget
{
    Q_OBJECT
public:
    explicit ShadeBar(QWidget* parent = nullptr);
    void setColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QList<QColor> m_shades;
};

/**
 * The "Color Picker" window shown after a color was picked: a Pick button, the
 * history of picked colors and the color as HEX, RGB, HSL and HSV, each with a
 * copy button.
 */
class ColorPickerWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPickerWindow(ColorNames::History* history,
                               QWidget* parent = nullptr);

    QColor color() const { return m_color; }
    /// Shows `color`; `copiedText` (if not empty) was just copied: tells so
    void setColor(const QColor& color, const QString& copiedText = QString());
    /// Puts text on the clipboard and shows "Copied to clipboard"
    void copyText(const QString& text);
    void rebuildHistory();
    /// Value shown in the row `name` ("hex", "rgb", "hsl", "hsv")
    QString rowValue(const QString& name) const;

signals:
    void pickRequested();

protected:
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void showBanner(const QString& text);
    void positionBanner();

    ColorNames::History* m_history;
    QColor m_color;
    QHBoxLayout* m_historyLayout = nullptr;
    ShadeBar* m_shadeBar = nullptr;
    QLabel* m_values[4] = {};
    QLabel* m_banner = nullptr;
    QTimer* m_bannerTimer = nullptr;
};
