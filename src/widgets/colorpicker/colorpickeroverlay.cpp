// SPDX-License-Identifier: GPL-3.0-or-later

#include "colorpickeroverlay.h"
#include "utils/colornames.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

namespace {
// Pixels sampled across the magnifier, odd so one pixel is in the middle
const int kZoomLevels[] = { 7, 11, 15, 21, 31, 45 };
const int kDefaultLevel = 2;
const int kMagnifierSize = 196; // logical px
} // namespace

ColorPickerOverlay::ColorPickerOverlay(const QPixmap& screenshot,
                                       QWidget* parent)
  : QWidget(parent)
  , m_image(screenshot.toImage().convertToFormat(QImage::Format_RGB32))
  , m_pixmap(screenshot)
  , m_zoomLevel(kDefaultLevel)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QColor ColorPickerOverlay::colorAt(const QPointF& p) const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return QColor();
    }
    const int x = qBound(0,
                         int(p.x() * m_image.width() / width()),
                         m_image.width() - 1);
    const int y = qBound(0,
                         int(p.y() * m_image.height() / height()),
                         m_image.height() - 1);
    return QColor(m_image.pixel(x, y));
}

void ColorPickerOverlay::setProbe(const QPointF& p)
{
    m_pos = p;
    update();
}

void ColorPickerOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.drawPixmap(rect(), m_pixmap);
    if (m_image.isNull()) {
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);

    // --- magnifier ---------------------------------------------------------
    const int n = kZoomLevels[m_zoomLevel];
    const double sx = double(m_image.width()) / width();
    const double sy = double(m_image.height()) / height();
    const int cx = qBound(0, int(m_pos.x() * sx), m_image.width() - 1);
    const int cy = qBound(0, int(m_pos.y() * sy), m_image.height() - 1);
    const int half = n / 2;
    const QRect magnifier(QPoint(int(m_pos.x()) - kMagnifierSize / 2,
                                 int(m_pos.y()) - kMagnifierSize / 2),
                          QSize(kMagnifierSize, kMagnifierSize));
    const double cell = double(kMagnifierSize) / n;

    QPainterPath clip;
    clip.addRoundedRect(magnifier, 6, 6);
    p.save();
    p.setClipPath(clip);
    p.fillRect(magnifier, QColor(30, 30, 30));
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int ix = cx - half + i;
            const int iy = cy - half + j;
            if (ix < 0 || iy < 0 || ix >= m_image.width() ||
                iy >= m_image.height()) {
                continue;
            }
            p.fillRect(QRectF(magnifier.x() + i * cell,
                              magnifier.y() + j * cell,
                              cell + 0.6,
                              cell + 0.6),
                       QColor(m_image.pixel(ix, iy)));
        }
    }
    if (cell >= 9) { // faint pixel grid
        p.setPen(QPen(QColor(0, 0, 0, 40), 1));
        for (int k = 1; k < n; ++k) {
            p.drawLine(QPointF(magnifier.x() + k * cell, magnifier.y()),
                       QPointF(magnifier.x() + k * cell, magnifier.bottom()));
            p.drawLine(QPointF(magnifier.x(), magnifier.y() + k * cell),
                       QPointF(magnifier.right(), magnifier.y() + k * cell));
        }
    }
    p.restore();
    // the pixel that will be picked
    const QRectF mid(magnifier.x() + half * cell,
                     magnifier.y() + half * cell,
                     cell,
                     cell);
    p.setPen(QPen(Qt::black, 2));
    p.drawRect(mid);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(mid.adjusted(1, 1, -1, -1));
    // frame
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 230), 3));
    p.drawRoundedRect(magnifier.adjusted(-1, -1, 1, 1), 7, 7);
    p.setPen(QPen(QColor(0, 0, 0, 70), 1));
    p.drawRoundedRect(magnifier.adjusted(-3, -3, 3, 3), 8, 8);

    // --- card with the color -----------------------------------------------
    const QColor color = QColor(m_image.pixel(cx, cy));
    QFont big = font();
    big.setBold(true);
    big.setPointSizeF(font().pointSizeF() * 1.35);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.95);
    const QString hex = ColorNames::hex(color).mid(1);
    const QString name = ColorNames::nameFor(color);
    const QFontMetrics fmBig(big), fmSmall(small);
    const int textW = qMax(fmBig.horizontalAdvance(hex),
                           fmSmall.horizontalAdvance(name));
    const QSize card(10 + 44 + 10 + textW + 14, 62);
    QPoint at(int(m_pos.x()) + 18, int(m_pos.y()) + 18);
    if (at.x() + card.width() > width()) {
        at.setX(int(m_pos.x()) - 18 - card.width());
    }
    if (at.y() + card.height() > height()) {
        at.setY(int(m_pos.y()) - 18 - card.height());
    }
    const QRect cardRect(at, card);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 40)); // soft shadow
    p.drawRoundedRect(cardRect.translated(0, 3).adjusted(-1, -1, 1, 1), 10, 10);
    p.setBrush(QColor(252, 252, 252));
    p.setPen(QPen(QColor(210, 210, 210), 1));
    p.drawRoundedRect(cardRect, 9, 9);
    const QRect sw(cardRect.x() + 9, cardRect.y() + 9, 44, 44);
    p.setPen(QPen(QColor(0, 0, 0, 40), 1));
    p.setBrush(color);
    p.drawRoundedRect(sw, 5, 5);
    p.setPen(QColor(40, 40, 40));
    p.setFont(big);
    p.drawText(QPoint(sw.right() + 12, cardRect.y() + 28), hex);
    p.setPen(QColor(130, 130, 130));
    p.setFont(small);
    p.drawText(QPoint(sw.right() + 12, cardRect.y() + 48), name);
}

void ColorPickerOverlay::mouseMoveEvent(QMouseEvent* e)
{
    m_pos = e->position();
    update();
}

void ColorPickerOverlay::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        emit picked(colorAt(e->position()));
    } else {
        emit cancelled();
    }
}

void ColorPickerOverlay::wheelEvent(QWheelEvent* e)
{
    const int steps = int(sizeof(kZoomLevels) / sizeof(kZoomLevels[0]));
    // wheel up = zoom in = fewer sampled pixels
    const int dir = e->angleDelta().y() > 0 ? -1 : 1;
    m_zoomLevel = qBound(0, m_zoomLevel + dir, steps - 1);
    update();
}

void ColorPickerOverlay::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        emit cancelled();
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        emit picked(colorAt(m_pos));
    } else {
        QWidget::keyPressEvent(e);
    }
}
