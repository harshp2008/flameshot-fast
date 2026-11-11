// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QWidget>

/**
 * Full-screen overlay of the stand-alone color picker. It shows a frozen
 * picture of one monitor with a pixel magnifier and a small card with the
 * color under the pointer (hex and name). Click or Enter picks, Esc or right
 * click cancels, the mouse wheel changes the zoom.
 */
class ColorPickerOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPickerOverlay(const QPixmap& screenshot,
                                QWidget* parent = nullptr);

    /// Color of the pixel under logical widget position `p`
    QColor colorAt(const QPointF& p) const;
    /// Moves the probe without a mouse (used by tests)
    void setProbe(const QPointF& p);
    QPointF probe() const { return m_pos; }

signals:
    void picked(const QColor& color);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QImage m_image;
    QPixmap m_pixmap;
    QPointF m_pos;
    int m_zoomLevel;
};
