// SPDX-License-Identifier: GPL-3.0-or-later

#include "colorpickerwindow.h"
#include "utils/ocr.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const char* kRowNames[4] = { "hex", "rgb", "hsl", "hsv" };
const char* kRowLabels[4] = { "HEX", "RGB", "HSL", "HSV" };

bool darkTheme(const QWidget* w)
{
    return w->palette().window().color().lightness() < 128;
}
} // namespace

// ---------------------------------------------------------------------------

SwatchButton::SwatchButton(const QColor& color, QWidget* parent)
  : QWidget(parent)
  , m_color(color)
{
    setFixedSize(34, 34);
    setCursor(Qt::PointingHandCursor);
    setToolTip(ColorNames::hex(color));
}

void SwatchButton::setSelected(bool selected)
{
    m_selected = selected;
    update();
}

void SwatchButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(5, 5, -5, -5);
    p.setPen(QPen(QColor(0, 0, 0, 50), 1));
    p.setBrush(m_color);
    p.drawEllipse(r);
    if (m_selected) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(palette().highlight().color(), 2));
        p.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }
}

void SwatchButton::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        emit clicked(m_color);
    }
}

void SwatchButton::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    QAction* remove = menu.addAction(tr("Remove"));
    if (menu.exec(e->globalPos()) == remove) {
        emit removeRequested(m_color);
    }
}

// ---------------------------------------------------------------------------

ShadeBar::ShadeBar(QWidget* parent)
  : QWidget(parent)
{
    setFixedWidth(22);
}

void ShadeBar::setColor(const QColor& color)
{
    m_shades = ColorNames::shades(color, 7);
    update();
}

void ShadeBar::paintEvent(QPaintEvent*)
{
    if (m_shades.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(rect(), 5, 5);
    p.setClipPath(clip);
    const double h = double(height()) / m_shades.size();
    for (int i = 0; i < m_shades.size(); ++i) {
        p.fillRect(QRectF(0, i * h, width(), h + 1), m_shades[i]);
    }
}

// ---------------------------------------------------------------------------

ColorPickerWindow::ColorPickerWindow(ColorNames::History* history,
                                     QWidget* parent)
  : QWidget(parent)
  , m_history(history)
{
    setWindowTitle(tr("Color Picker"));
    setMinimumSize(470, 390);
    resize(500, 400);

    const bool dark = darkTheme(this);
    const QString panel = dark ? "#2b2b2b" : "#ffffff";
    const QString line = dark ? "#3d3d3d" : "#e3e3e3";
    const QString sub = dark ? "#9a9a9a" : "#767676";
    const QString bar = dark ? "#202020" : "#f3f6f9";
    setStyleSheet(QStringLiteral(
                    "ColorPickerWindow { background: %1; }"
                    "QFrame#row { background: %1; border: 1px solid %2;"
                    "  border-radius: 6px; }"
                    "QLabel#rowLabel { color: %3; font-size: 9pt; }"
                    "QLabel#rowValue { font-size: 11pt; }"
                    "QFrame#top { background: %4; border: none; }"
                    "QPushButton#pick { background: %1; border: 1px solid %2;"
                    "  border-radius: 6px; padding: 6px 14px; }"
                    "QPushButton#pick:hover { border-color: %3; }"
                    "QToolButton { border: none; border-radius: 5px;"
                    "  padding: 5px; }"
                    "QToolButton:hover { background: %2; }"
                    "QScrollArea { border: none; background: transparent; }"
                    "QScrollArea > QWidget > QWidget { background: transparent; }")
                    .arg(panel, line, sub, bar));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // top bar: Pick + history
    auto* top = new QFrame(this);
    top->setObjectName("top");
    auto* topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(14, 10, 14, 10);
    auto* pick = new QPushButton(QIcon::fromTheme("color-select"),
                                 tr("Pick"),
                                 top);
    pick->setObjectName("pick");
    pick->setCursor(Qt::PointingHandCursor);
    connect(pick, &QPushButton::clicked, this, &ColorPickerWindow::pickRequested);
    topLayout->addWidget(pick);

    auto* scroll = new QScrollArea(top);
    scroll->setWidgetResizable(true);
    scroll->setFixedHeight(48);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* strip = new QWidget();
    m_historyLayout = new QHBoxLayout(strip);
    m_historyLayout->setContentsMargins(0, 0, 0, 0);
    m_historyLayout->setSpacing(2);
    scroll->setWidget(strip);
    topLayout->addWidget(scroll, 1);
    root->addWidget(top);

    // body: shade bar + value rows
    auto* body = new QHBoxLayout();
    body->setContentsMargins(16, 16, 16, 16);
    body->setSpacing(14);
    m_shadeBar = new ShadeBar(this);
    body->addWidget(m_shadeBar);
    auto* rows = new QVBoxLayout();
    rows->setSpacing(10);
    for (int i = 0; i < 4; ++i) {
        auto* row = new QFrame(this);
        row->setObjectName("row");
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(14, 10, 8, 10);
        auto* label = new QLabel(QString::fromLatin1(kRowLabels[i]), row);
        label->setObjectName("rowLabel");
        label->setFixedWidth(56);
        m_values[i] = new QLabel(row);
        m_values[i]->setObjectName("rowValue");
        m_values[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* copy = new QToolButton(row);
        copy->setIcon(QIcon::fromTheme("edit-copy"));
        if (copy->icon().isNull()) {
            copy->setText(tr("Copy"));
        }
        copy->setToolTip(tr("Copy"));
        copy->setCursor(Qt::PointingHandCursor);
        connect(copy, &QToolButton::clicked, this, [this, i]() {
            copyText(m_values[i]->text());
        });
        l->addWidget(label);
        l->addWidget(m_values[i], 1);
        l->addWidget(copy);
        rows->addWidget(row);
    }
    rows->addStretch(1);
    body->addLayout(rows, 1);
    root->addLayout(body, 1);

    m_banner = new QLabel(tr("Copied to clipboard"), this);
    m_banner->setAlignment(Qt::AlignCenter);
    m_banner->setStyleSheet(
      "background: #3f8f9e; color: white; font-size: 11pt;"
      "border-bottom-left-radius: 6px; border-bottom-right-radius: 6px;");
    m_banner->hide();
    m_bannerTimer = new QTimer(this);
    m_bannerTimer->setSingleShot(true);
    connect(m_bannerTimer, &QTimer::timeout, m_banner, &QLabel::hide);

    rebuildHistory();
}

void ColorPickerWindow::rebuildHistory()
{
    while (QLayoutItem* it = m_historyLayout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (const QColor& c : m_history->colors()) {
        auto* b = new SwatchButton(c);
        b->setSelected(c == m_color);
        connect(b, &SwatchButton::clicked, this, [this](const QColor& col) {
            setColor(col);
        });
        connect(b, &SwatchButton::removeRequested, this, [this](const QColor& col) {
            m_history->remove(col);
            rebuildHistory();
        });
        m_historyLayout->addWidget(b);
    }
    m_historyLayout->addStretch(1);
}

void ColorPickerWindow::setColor(const QColor& color, const QString& copiedText)
{
    m_color = color;
    m_shadeBar->setColor(color);
    for (int i = 0; i < 4; ++i) {
        m_values[i]->setText(
          ColorNames::format(color, QString::fromLatin1(kRowNames[i])));
    }
    for (int i = 0; i < m_historyLayout->count(); ++i) {
        if (auto* b = qobject_cast<SwatchButton*>(
              m_historyLayout->itemAt(i)->widget())) {
            b->setSelected(b->color() == color);
        }
    }
    if (!copiedText.isEmpty()) {
        showBanner(tr("Copied to clipboard"));
    }
}

QString ColorPickerWindow::rowValue(const QString& name) const
{
    for (int i = 0; i < 4; ++i) {
        if (name == QLatin1String(kRowNames[i])) {
            return m_values[i]->text();
        }
    }
    return QString();
}

void ColorPickerWindow::copyText(const QString& text)
{
    if (!Ocr::copyTextPersistent(text)) {
        QApplication::clipboard()->setText(text);
    }
    showBanner(tr("Copied to clipboard"));
}

void ColorPickerWindow::showBanner(const QString& text)
{
    m_banner->setText(text);
    positionBanner();
    m_banner->show();
    m_banner->raise();
    m_bannerTimer->start(1800);
}

void ColorPickerWindow::positionBanner()
{
    const int h = 46;
    m_banner->setGeometry(0, height() - h, width(), h);
}

void ColorPickerWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    positionBanner();
}

void ColorPickerWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
    } else {
        QWidget::keyPressEvent(e);
    }
}
