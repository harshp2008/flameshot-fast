// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

/**
 * One monitor stream as reported by the ScreenCast portal's Start response.
 * Position and size are in logical (compositor) coordinates.
 */
struct ScreenCastStream
{
    quint32 nodeId = 0;
    QPoint position;
    QSize size;
};

/// Pixel layouts accepted from PipeWire (little endian byte order in memory).
enum class ScreenCastPixelFormat
{
    BGRx,
    BGRA,
    RGBx,
    RGBA
};

/**
 * Optional backend that captures single monitors through the ScreenCast portal
 * and PipeWire, as an alternative to the (slow on multi-monitor setups)
 * Screenshot portal. Every failure is reported through Result::error so the
 * caller can fall back to the regular capture path.
 */
class ScreenCastGrabber : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        QString restoreToken; ///< token saved by a previous successful Start
        bool embedCursor = false;
        /// Parent window handle passed to Start ("wayland:" when unknown)
        QString parentWindow = QStringLiteral("wayland:");
        int portalTimeoutMs = 3000;       ///< per portal step
        int interactiveTimeoutMs = 60000; ///< Start without a restore token
        int frameTimeoutMs = 2500;        ///< waiting for the first frame
    };

    struct Result
    {
        /// Image per requested screen index. Device pixel ratio is set to
        /// frame width / logical width of the screen.
        QHash<int, QImage> images;
        /// New token to persist (also set when the capture itself failed)
        QString restoreToken;
        /// True when the saved token is no longer useful (e.g. it does not
        /// cover every monitor) and should be cleared
        bool forgetToken = false;
        /// Empty on success
        QString error;
        bool ok() const { return error.isEmpty(); }
    };

    explicit ScreenCastGrabber(QObject* parent = nullptr);

    /// True when Flameshot was built with libpipewire support
    static bool compiledIn();

    /**
     * Grab the screens `wanted` (indices into `screenGeometries`, the logical
     * QScreen geometries in the order of QGuiApplication::screens()).
     */
    Result grab(const QList<QRect>& screenGeometries,
                const QList<int>& wanted,
                const Options& options);

    // Pure helpers, public so they can be unit tested without a session.

    /**
     * For each screen geometry returns the index of the stream with the same
     * logical position and size (tolerance `tolerance` px per value), or -1.
     * A stream is never assigned to two screens; ambiguous matches give -1.
     */
    static QList<int> matchStreamsToScreens(
      const QList<ScreenCastStream>& streams,
      const QList<QRect>& screenGeometries,
      int tolerance = 2);

    /// Parses "x, y" (any non-digit separator) into a point
    static bool parsePointer(const QString& text, QPoint& out);

    /// Index of the screen geometry containing `p`, or -1
    static int monitorAt(const QList<QRect>& screenGeometries, const QPoint& p);

    /**
     * Copies `data` (rows of `stride` bytes) into a new QImage. Returns a null
     * image when size, stride or the buffer length are inconsistent.
     */
    static QImage imageFromBuffer(ScreenCastPixelFormat format,
                                  const QSize& size,
                                  int stride,
                                  const uchar* data,
                                  qint64 dataSize);
};
