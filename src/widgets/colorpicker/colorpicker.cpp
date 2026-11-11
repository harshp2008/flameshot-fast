// SPDX-License-Identifier: GPL-3.0-or-later

#include "colorpicker.h"
#include "colorpickeroverlay.h"
#include "colorpickerwindow.h"
#include "utils/abstractlogger.h"
#include "utils/colornames.h"
#include "utils/confighandler.h"
#include "utils/desktopinfo.h"
#include "utils/ocr.h"
#include "utils/screengrabber.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWindow>

namespace {

class ColorPickerFlow : public QObject
{
public:
    ColorPickerFlow()
      : m_window(&m_history)
    {
        connect(&m_window,
                &ColorPickerWindow::pickRequested,
                this,
                &ColorPickerFlow::startPicking);
    }

    bool startPicking()
    {
        const bool windowWasOpen = m_window.isVisible();
        if (windowWasOpen) {
            m_window.hide();
            Ocr::pause(350); // let the compositor repaint before the grab
        }
        ScreenGrabber grabber;
        bool ok = true;
        const QPixmap shot = grabber.grabEntireDesktop(ok, -1);
        if (!ok || shot.isNull()) {
            AbstractLogger::error() << QObject::tr("Unable to capture screen");
            if (windowWasOpen) {
                m_window.show();
            } else {
                qApp->exit(1);
            }
            return false;
        }
        QScreen* screen = grabber.getSelectedScreen();
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }

        m_overlay = new ColorPickerOverlay(shot);
        m_overlay->setAttribute(Qt::WA_DeleteOnClose);
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
        if (DesktopInfo().waylandDetected()) {
            m_overlay->setWindowFlags(Qt::BypassWindowManagerHint |
                                      Qt::WindowStaysOnTopHint |
                                      Qt::FramelessWindowHint | Qt::Tool);
        } else {
            m_overlay->setWindowFlags(Qt::WindowStaysOnTopHint |
                                      Qt::FramelessWindowHint | Qt::Tool);
        }
#endif
        m_overlay->move(screen->geometry().topLeft());
        m_overlay->resize(screen->geometry().size());
        connect(m_overlay,
                &ColorPickerOverlay::picked,
                this,
                &ColorPickerFlow::onPicked);
        connect(m_overlay,
                &ColorPickerOverlay::cancelled,
                this,
                &ColorPickerFlow::onCancelled);
        m_overlay->showFullScreen();
        if (m_overlay->windowHandle()) {
            m_overlay->windowHandle()->setScreen(screen);
        }
        m_overlay->activateWindow();
        m_overlay->raise();
        m_overlay->setFocus(Qt::ActiveWindowFocusReason);
        m_overlay->setProbe(QPointF(m_overlay->width() / 2.0,
                                    m_overlay->height() / 2.0));
        return true;
    }

    // Debug/test hook: render both widgets to PNG files without a person
    void runScripted(const QString& dir, const QPointF& at)
    {
        if (!startPicking() || !m_overlay) {
            return;
        }
        QTimer::singleShot(300, this, [this, dir, at]() {
            QDir().mkpath(dir);
            m_overlay->setProbe(at);
            QCoreApplication::processEvents();
            m_overlay->grab().save(dir + "/overlay.png");
            const QColor c = m_overlay->colorAt(at);
            QTextStream(stdout) << "picked " << ColorNames::hex(c) << " "
                                << ColorNames::nameFor(c) << "\n";
            onPicked(c);
            QCoreApplication::processEvents();
            m_window.grab().save(dir + "/window.png");
            QTextStream(stdout)
              << "hex=" << m_window.rowValue("hex")
              << " rgb=" << m_window.rowValue("rgb")
              << " hsl=" << m_window.rowValue("hsl")
              << " hsv=" << m_window.rowValue("hsv") << "\n";
            qApp->exit(0);
        });
    }

private:
    void onPicked(const QColor& color)
    {
        m_picked = true;
        m_history.add(color);
        const QString text =
          ColorNames::format(color, ConfigHandler().colorPickerFormat());
        if (!Ocr::copyTextPersistent(text)) {
            QApplication::clipboard()->setText(text);
        }
        m_window.rebuildHistory();
        m_window.setColor(color, text);
        m_window.show();
        m_window.raise();
        m_window.activateWindow();
        if (m_overlay) {
            m_overlay->close();
            m_overlay = nullptr;
        }
    }

    void onCancelled()
    {
        if (m_overlay) {
            m_overlay->close();
            m_overlay = nullptr;
        }
        if (m_picked) {
            m_window.show(); // came from the window's Pick button
        } else {
            qApp->exit(1);
        }
    }

    ColorNames::History m_history;
    ColorPickerWindow m_window;
    QPointer<ColorPickerOverlay> m_overlay;
    bool m_picked = false;
};

} // namespace

int runColorPicker()
{
    ColorPickerFlow flow;
    const QByteArray dump = qgetenv("FLAMESHOT_COLORPICKER_DUMP");
    if (!dump.isEmpty()) {
        const QStringList xy =
          QString::fromLocal8Bit(qgetenv("FLAMESHOT_COLORPICKER_AT"))
            .split(',');
        flow.runScripted(
          QString::fromLocal8Bit(dump),
          xy.size() == 2 ? QPointF(xy[0].toDouble(), xy[1].toDouble())
                         : QPointF(100, 100));
    } else if (!flow.startPicking()) {
        return 1;
    }
    return qApp->exec();
}
