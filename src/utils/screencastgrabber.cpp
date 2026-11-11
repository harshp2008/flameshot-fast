// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "screencastgrabber.h"

#include <QRegularExpression>
#include <QtGlobal>
#include <cstring>

#ifdef FLAMESHOT_HAVE_PIPEWIRE
#include "request.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QEventLoop>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>
#endif

ScreenCastGrabber::ScreenCastGrabber(QObject* parent)
  : QObject(parent)
{}

bool ScreenCastGrabber::compiledIn()
{
#ifdef FLAMESHOT_HAVE_PIPEWIRE
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

QList<int> ScreenCastGrabber::matchStreamsToScreens(
  const QList<ScreenCastStream>& streams,
  const QList<QRect>& screenGeometries,
  int tolerance)
{
    QList<int> result;
    for (const QRect& geo : screenGeometries) {
        QList<int> close;
        QList<int> exact;
        for (int i = 0; i < streams.size(); ++i) {
            const ScreenCastStream& s = streams.at(i);
            if (qAbs(s.position.x() - geo.x()) > tolerance ||
                qAbs(s.position.y() - geo.y()) > tolerance ||
                qAbs(s.size.width() - geo.width()) > tolerance ||
                qAbs(s.size.height() - geo.height()) > tolerance) {
                continue;
            }
            close.append(i);
            if (s.position == geo.topLeft() && s.size == geo.size()) {
                exact.append(i);
            }
        }
        if (exact.size() == 1) {
            result.append(exact.first());
        } else if (exact.isEmpty() && close.size() == 1) {
            result.append(close.first());
        } else {
            result.append(-1);
        }
    }
    // A stream must belong to exactly one screen
    for (int i = 0; i < result.size(); ++i) {
        for (int j = i + 1; j < result.size(); ++j) {
            if (result.at(i) >= 0 && result.at(i) == result.at(j)) {
                const int dup = result.at(i);
                for (int& r : result) {
                    if (r == dup) {
                        r = -1;
                    }
                }
            }
        }
    }
    return result;
}

bool ScreenCastGrabber::parsePointer(const QString& text, QPoint& out)
{
    static const QRegularExpression re(
      QStringLiteral("^\\s*(-?\\d+)[^\\d-]+(-?\\d+)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return false;
    }
    out = QPoint(m.captured(1).toInt(), m.captured(2).toInt());
    return true;
}

int ScreenCastGrabber::monitorAt(const QList<QRect>& screenGeometries,
                                 const QPoint& p)
{
    for (int i = 0; i < screenGeometries.size(); ++i) {
        if (screenGeometries.at(i).contains(p)) {
            return i;
        }
    }
    return -1;
}

QImage ScreenCastGrabber::imageFromBuffer(ScreenCastPixelFormat format,
                                          const QSize& size,
                                          int stride,
                                          const uchar* data,
                                          qint64 dataSize)
{
    if (!data || size.width() <= 0 || size.height() <= 0 ||
        size.width() > 32768 || size.height() > 32768) {
        return QImage();
    }
    const qint64 rowBytes = qint64(size.width()) * 4;
    if (stride < rowBytes) {
        return QImage();
    }
    if (dataSize < qint64(stride) * (size.height() - 1) + rowBytes) {
        return QImage();
    }

    QImage::Format qtFormat = QImage::Format_Invalid;
    switch (format) {
        case ScreenCastPixelFormat::BGRx:
            qtFormat = QImage::Format_RGB32;
            break;
        case ScreenCastPixelFormat::BGRA:
            qtFormat = QImage::Format_ARGB32_Premultiplied;
            break;
        case ScreenCastPixelFormat::RGBx:
            qtFormat = QImage::Format_RGBX8888;
            break;
        case ScreenCastPixelFormat::RGBA:
            qtFormat = QImage::Format_RGBA8888_Premultiplied;
            break;
    }
    QImage image(size, qtFormat);
    if (image.isNull()) {
        return QImage();
    }
    for (int y = 0; y < size.height(); ++y) {
        std::memcpy(image.scanLine(y), data + qint64(y) * stride, rowBytes);
    }
    if (format == ScreenCastPixelFormat::RGBx) {
        // Qt reads the unused byte of RGBX8888 as alpha in some code paths
        return image.convertToFormat(QImage::Format_RGB32);
    }
    return image;
}

#ifndef FLAMESHOT_HAVE_PIPEWIRE

ScreenCastGrabber::Result ScreenCastGrabber::grab(const QList<QRect>&,
                                                  const QList<int>&,
                                                  const Options&)
{
    Result r;
    r.error =
      tr("Flameshot was built without ScreenCast (libpipewire) support");
    return r;
}

#else // FLAMESHOT_HAVE_PIPEWIRE

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenCast = QStringLiteral("org.freedesktop.portal.ScreenCast");

QString newToken()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

struct PortalResponse
{
    QVariantMap results;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

/// Calls a portal method that answers through a Request/Response signal.
/// The Response listener is installed before the call (no race).
PortalResponse callRequest(const QString& method,
                           const QList<QVariant>& args,
                           QVariantMap options,
                           int timeoutMs)
{
    PortalResponse out;
    const QString token = newToken();
    options.insert(QStringLiteral("handle_token"), token);
    const QString expectedPath =
      QStringLiteral("/org/freedesktop/portal/desktop/request/") +
      QDBusConnection::sessionBus().baseService().remove(':').replace('.',
                                                                      '_') +
      QLatin1Char('/') + token;

    OrgFreedesktopPortalRequestInterface request(
      kService, expectedPath, QDBusConnection::sessionBus());

    QEventLoop loop;
    bool answered = false;
    uint code = 2;
    QVariantMap results;
    QObject::connect(&request,
                     &org::freedesktop::portal::Request::Response,
                     &loop,
                     [&](uint c, const QVariantMap& r) {
                         answered = true;
                         code = c;
                         results = r;
                         loop.quit();
                     });
    bool timedOut = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    QDBusMessage msg =
      QDBusMessage::createMethodCall(kService, kPath, kScreenCast, method);
    QList<QVariant> all = args;
    all.append(options);
    msg.setArguments(all);
    const QDBusMessage reply =
      QDBusConnection::sessionBus().call(msg, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        out.error = QObject::tr("ScreenCast %1 failed: %2")
                      .arg(method, reply.errorMessage());
        return out;
    }
    if (!reply.arguments().isEmpty() &&
        reply.arguments().first().value<QDBusObjectPath>().path() !=
          expectedPath) {
        out.error =
          QObject::tr("ScreenCast %1: unexpected request handle").arg(method);
        return out;
    }

    timer.start(timeoutMs);
    loop.exec();
    timer.stop();

    if (timedOut && !answered) {
        request.Close().waitForFinished();
        out.error = QObject::tr("ScreenCast %1 timed out after %2 ms")
                      .arg(method)
                      .arg(timeoutMs);
    } else if (code == 1) {
        out.error =
          QObject::tr("ScreenCast %1 cancelled by the user").arg(method);
    } else if (code != 0) {
        out.error = QObject::tr("ScreenCast %1 ended with response code %2")
                      .arg(method)
                      .arg(code);
    } else {
        out.results = results;
    }
    return out;
}

/// Closes the portal session on every path out of grab().
struct SessionGuard
{
    QString handle;
    void close()
    {
        if (handle.isEmpty()) {
            return;
        }
        QDBusMessage m = QDBusMessage::createMethodCall(
          kService,
          handle,
          QStringLiteral("org.freedesktop.portal.Session"),
          QStringLiteral("Close"));
        QDBusConnection::sessionBus().call(m, QDBus::Block, 500);
        handle.clear();
    }
    ~SessionGuard() { close(); }
};

bool readPair(const QVariant& v, int& a, int& b)
{
    const QDBusArgument arg = v.value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::StructureType) {
        return false;
    }
    arg.beginStructure();
    arg >> a >> b;
    arg.endStructure();
    return true;
}

QList<ScreenCastStream> parseStreams(const QVariant& streamsVariant)
{
    QList<ScreenCastStream> out;
    const QDBusArgument arg = streamsVariant.value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) {
        return out;
    }
    arg.beginArray();
    while (!arg.atEnd()) {
        ScreenCastStream stream;
        bool havePos = false;
        bool haveSize = false;
        arg.beginStructure();
        arg >> stream.nodeId;
        arg.beginMap();
        while (!arg.atEnd()) {
            QString key;
            QDBusVariant value;
            arg.beginMapEntry();
            arg >> key >> value;
            arg.endMapEntry();
            int a = 0;
            int b = 0;
            if (key == QLatin1String("position") &&
                readPair(value.variant(), a, b)) {
                stream.position = QPoint(a, b);
                havePos = true;
            } else if (key == QLatin1String("size") &&
                       readPair(value.variant(), a, b)) {
                stream.size = QSize(a, b);
                haveSize = true;
            }
        }
        arg.endMap();
        arg.endStructure();
        if (havePos && haveSize) {
            out.append(stream);
        }
    }
    arg.endArray();
    return out;
}

/// Reads the first frame of several PipeWire nodes. Runs the PipeWire loop
/// inside the calling (Qt) thread through a QSocketNotifier, so no thread or
/// lock is needed. Not reentrant; use one instance per capture.
class PwCapture
{
public:
    struct Slot
    {
        PwCapture* owner = nullptr;
        int screenIndex = -1;
        quint32 nodeId = 0;
        pw_stream* stream = nullptr;
        spa_hook listener{};
        spa_video_info_raw info{};
        ScreenCastPixelFormat format = ScreenCastPixelFormat::BGRx;
        bool done = false;
        QImage image;
        QString error;

        void fail(const QString& e)
        {
            if (!done) {
                done = true;
                error = e;
            }
            owner->check();
        }
    };

    ~PwCapture() { teardown(); }

    void addStream(int screenIndex, quint32 nodeId)
    {
        auto slot = std::make_unique<Slot>();
        slot->owner = this;
        slot->screenIndex = screenIndex;
        slot->nodeId = nodeId;
        m_slots.push_back(std::move(slot));
    }

    /// Takes ownership of `fd`. Returns an error string, empty on success
    /// (per-stream errors stay in the slots).
    QString run(int fd, int timeoutMs)
    {
        static std::once_flag once;
        std::call_once(once, []() { pw_init(nullptr, nullptr); });

        m_loop = pw_loop_new(nullptr);
        if (!m_loop) {
            ::close(fd);
            return QObject::tr("pw_loop_new failed");
        }
        pw_loop_enter(m_loop);
        m_context = pw_context_new(m_loop, nullptr, 0);
        if (!m_context) {
            ::close(fd);
            return QObject::tr("pw_context_new failed");
        }
        m_core = pw_context_connect_fd(m_context, fd, nullptr, 0);
        if (!m_core) {
            return QObject::tr("Could not connect to the PipeWire remote");
        }
        static const pw_core_events coreEvents = {
            .version = PW_VERSION_CORE_EVENTS,
            .error = &PwCapture::onCoreError,
        };
        pw_core_add_listener(m_core, &m_coreListener, &coreEvents, this);
        m_haveCoreListener = true;

        for (auto& slot : m_slots) {
            if (QString e = connectStream(*slot); !e.isEmpty()) {
                return e;
            }
        }

        QSocketNotifier notifier(pw_loop_get_fd(m_loop), QSocketNotifier::Read);
        QObject::connect(&notifier,
                         &QSocketNotifier::activated,
                         &m_eventLoop,
                         [this]() { iterate(); });
        QTimer timer;
        timer.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&timer, &QTimer::timeout, &m_eventLoop, [&]() {
            timedOut = true;
            m_eventLoop.quit();
        });
        timer.start(timeoutMs);
        QTimer::singleShot(0, &m_eventLoop, [this]() { iterate(); });
        if (!allDone()) {
            m_eventLoop.exec();
        }
        timer.stop();
        if (timedOut && !allDone()) {
            return QObject::tr("No video frame within %1 ms").arg(timeoutMs);
        }
        return QString();
    }

    const std::vector<std::unique_ptr<Slot>>& streamSlots() const
    {
        return m_slots;
    }

    void check()
    {
        if (allDone()) {
            m_eventLoop.quit();
        }
    }

private:
    bool allDone() const
    {
        for (const auto& s : m_slots) {
            if (!s->done) {
                return false;
            }
        }
        return true;
    }

    void iterate()
    {
        if (m_loop) {
            pw_loop_iterate(m_loop, 0);
        }
    }

    QString connectStream(Slot& slot)
    {
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE,
                                                 "Video",
                                                 PW_KEY_MEDIA_CATEGORY,
                                                 "Capture",
                                                 PW_KEY_MEDIA_ROLE,
                                                 "Screen",
                                                 nullptr);
        slot.stream = pw_stream_new(m_core, "flameshot-screencast", props);
        if (!slot.stream) {
            return QObject::tr("pw_stream_new failed");
        }
        static const pw_stream_events events = {
            .version = PW_VERSION_STREAM_EVENTS,
            .state_changed = &PwCapture::onStateChanged,
            .param_changed = &PwCapture::onParamChanged,
            .process = &PwCapture::onProcess,
        };
        pw_stream_add_listener(slot.stream, &slot.listener, &events, &slot);

        uint8_t podBuffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));
        const spa_rectangle minSize{ 1, 1 };
        const spa_rectangle maxSize{ 32768, 32768 };
        const spa_rectangle defSize{ 1920, 1080 };
        const spa_fraction minRate{ 0, 1 };
        const spa_fraction maxRate{ 1000, 1 };
        const spa_fraction defRate{ 30, 1 };
        const spa_pod* params[1];
        params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
          &b,
          SPA_TYPE_OBJECT_Format,
          SPA_PARAM_EnumFormat,
          SPA_FORMAT_mediaType,
          SPA_POD_Id(SPA_MEDIA_TYPE_video),
          SPA_FORMAT_mediaSubtype,
          SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
          SPA_FORMAT_VIDEO_format,
          SPA_POD_CHOICE_ENUM_Id(5,
                                 SPA_VIDEO_FORMAT_BGRx,
                                 SPA_VIDEO_FORMAT_BGRx,
                                 SPA_VIDEO_FORMAT_BGRA,
                                 SPA_VIDEO_FORMAT_RGBx,
                                 SPA_VIDEO_FORMAT_RGBA),
          SPA_FORMAT_VIDEO_size,
          SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize),
          SPA_FORMAT_VIDEO_framerate,
          SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate)));

        const int rc = pw_stream_connect(
          slot.stream,
          PW_DIRECTION_INPUT,
          slot.nodeId,
          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                       PW_STREAM_FLAG_MAP_BUFFERS),
          params,
          1);
        if (rc < 0) {
            return QObject::tr("pw_stream_connect failed: %1")
              .arg(QString::fromUtf8(strerror(-rc)));
        }
        return QString();
    }

    void teardown()
    {
        for (auto& slot : m_slots) {
            if (slot->stream) {
                spa_hook_remove(&slot->listener);
                pw_stream_destroy(slot->stream);
                slot->stream = nullptr;
            }
        }
        if (m_haveCoreListener) {
            spa_hook_remove(&m_coreListener);
            m_haveCoreListener = false;
        }
        if (m_core) {
            pw_core_disconnect(m_core);
            m_core = nullptr;
        }
        if (m_context) {
            pw_context_destroy(m_context);
            m_context = nullptr;
        }
        if (m_loop) {
            pw_loop_leave(m_loop);
            pw_loop_destroy(m_loop);
            m_loop = nullptr;
        }
    }

    static void onCoreError(void* data,
                            uint32_t id,
                            int seq,
                            int res,
                            const char* message)
    {
        Q_UNUSED(seq)
        auto* self = static_cast<PwCapture*>(data);
        if (id != PW_ID_CORE) {
            return;
        }
        const QString e = QObject::tr("PipeWire core error %1: %2")
                            .arg(res)
                            .arg(QString::fromUtf8(message));
        for (auto& s : self->m_slots) {
            if (!s->done) {
                s->done = true;
                s->error = e;
            }
        }
        self->check();
    }

    static void onStateChanged(void* data,
                               pw_stream_state,
                               pw_stream_state state,
                               const char* error)
    {
        auto* slot = static_cast<Slot*>(data);
        if (state == PW_STREAM_STATE_ERROR) {
            slot->fail(QObject::tr("PipeWire stream error: %1")
                         .arg(QString::fromUtf8(error ? error : "unknown")));
        }
    }

    static void onParamChanged(void* data, uint32_t id, const spa_pod* param)
    {
        auto* slot = static_cast<Slot*>(data);
        if (!param || id != SPA_PARAM_Format) {
            return;
        }
        uint32_t mediaType = 0;
        uint32_t mediaSubtype = 0;
        if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0) {
            return;
        }
        if (mediaType != SPA_MEDIA_TYPE_video ||
            mediaSubtype != SPA_MEDIA_SUBTYPE_raw) {
            slot->fail(QObject::tr("Stream is not raw video"));
            return;
        }
        spa_format_video_raw_parse(param, &slot->info);
        switch (slot->info.format) {
            case SPA_VIDEO_FORMAT_BGRx:
                slot->format = ScreenCastPixelFormat::BGRx;
                break;
            case SPA_VIDEO_FORMAT_BGRA:
                slot->format = ScreenCastPixelFormat::BGRA;
                break;
            case SPA_VIDEO_FORMAT_RGBx:
                slot->format = ScreenCastPixelFormat::RGBx;
                break;
            case SPA_VIDEO_FORMAT_RGBA:
                slot->format = ScreenCastPixelFormat::RGBA;
                break;
            default:
                slot->fail(QObject::tr("Unsupported pixel format %1")
                             .arg(slot->info.format));
                return;
        }
        // Only shared memory buffers: no DMA-BUF, so the first buffer can be
        // read with a plain memcpy.
        uint8_t podBuffer[512];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));
        const spa_pod* params[1];
        params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(
          &b,
          SPA_TYPE_OBJECT_ParamBuffers,
          SPA_PARAM_Buffers,
          SPA_PARAM_BUFFERS_buffers,
          SPA_POD_CHOICE_RANGE_Int(4, 1, 8),
          SPA_PARAM_BUFFERS_blocks,
          SPA_POD_Int(1),
          SPA_PARAM_BUFFERS_dataType,
          SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) |
                                   (1 << SPA_DATA_MemPtr))));
        pw_stream_update_params(slot->stream, params, 1);
    }

    static void onProcess(void* data)
    {
        auto* slot = static_cast<Slot*>(data);
        while (pw_buffer* pb = pw_stream_dequeue_buffer(slot->stream)) {
            if (!slot->done) {
                takeFrame(*slot, pb->buffer);
            }
            pw_stream_queue_buffer(slot->stream, pb);
        }
    }

    static void takeFrame(Slot& slot, const spa_buffer* buf)
    {
        if (buf->n_datas < 1) {
            return;
        }
        const spa_data& d = buf->datas[0];
        if (!d.data || !d.chunk || d.chunk->size == 0 ||
            (d.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED)) {
            return; // empty/damage-only buffer: wait for the next one
        }
        if (d.type != SPA_DATA_MemFd && d.type != SPA_DATA_MemPtr) {
            slot.fail(QObject::tr("Unexpected buffer type %1").arg(d.type));
            return;
        }
        if (auto* h = static_cast<spa_meta_header*>(spa_buffer_find_meta_data(
              buf, SPA_META_Header, sizeof(spa_meta_header)));
            h && (h->flags & SPA_META_HEADER_FLAG_CORRUPTED)) {
            return;
        }
        const QSize size(slot.info.size.width, slot.info.size.height);
        int stride = d.chunk->stride;
        if (stride <= 0) {
            stride = size.width() * 4;
        }
        const qint64 offset = d.chunk->offset;
        if (offset >= qint64(d.maxsize)) {
            slot.fail(QObject::tr("Buffer offset out of range"));
            return;
        }
        const QImage img = ScreenCastGrabber::imageFromBuffer(
          slot.format,
          size,
          stride,
          static_cast<const uchar*>(d.data) + offset,
          qint64(d.maxsize) - offset);
        if (img.isNull()) {
            slot.fail(
              QObject::tr("Inconsistent frame buffer (%1x%2, stride %3)")
                .arg(size.width())
                .arg(size.height())
                .arg(stride));
            return;
        }
        slot.image = img;
        slot.done = true;
        slot.owner->check();
    }

    pw_loop* m_loop = nullptr;
    pw_context* m_context = nullptr;
    pw_core* m_core = nullptr;
    spa_hook m_coreListener{};
    bool m_haveCoreListener = false;
    QEventLoop m_eventLoop;
    std::vector<std::unique_ptr<Slot>> m_slots;
};

} // namespace

ScreenCastGrabber::Result ScreenCastGrabber::grab(
  const QList<QRect>& screenGeometries,
  const QList<int>& wanted,
  const Options& options)
{
    Result r;
    if (wanted.isEmpty()) {
        r.error = tr("No screen requested");
        return r;
    }
    for (int idx : wanted) {
        if (idx < 0 || idx >= screenGeometries.size()) {
            r.error = tr("Screen index %1 out of range").arg(idx);
            return r;
        }
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        r.error = tr("No D-Bus session bus");
        return r;
    }

    // 0. Is there a ScreenCast portal that can remember permissions?
    {
        QDBusMessage get = QDBusMessage::createMethodCall(
          kService,
          kPath,
          QStringLiteral("org.freedesktop.DBus.Properties"),
          QStringLiteral("Get"));
        get.setArguments({ kScreenCast, QStringLiteral("version") });
        const QDBusMessage reply =
          bus.call(get, QDBus::Block, options.portalTimeoutMs);
        if (reply.type() == QDBusMessage::ErrorMessage ||
            reply.arguments().isEmpty()) {
            r.error =
              tr("ScreenCast portal unavailable: %1").arg(reply.errorMessage());
            return r;
        }
        const uint version =
          reply.arguments().first().value<QDBusVariant>().variant().toUInt();
        if (version < 4) {
            r.error = tr("ScreenCast portal version %1 cannot restore "
                         "permissions (need 4)")
                        .arg(version);
            return r;
        }
    }

    SessionGuard session;

    // 1. CreateSession
    {
        const PortalResponse resp = callRequest(
          QStringLiteral("CreateSession"),
          {},
          { { QStringLiteral("session_handle_token"), newToken() } },
          options.portalTimeoutMs);
        if (!resp.ok()) {
            r.error = resp.error;
            return r;
        }
        const QVariant h = resp.results.value(QStringLiteral("session_handle"));
        session.handle = h.metaType() == QMetaType::fromType<QDBusObjectPath>()
                           ? h.value<QDBusObjectPath>().path()
                           : h.toString();
        if (session.handle.isEmpty()) {
            r.error = tr("CreateSession returned no session handle");
            return r;
        }
    }
    const QVariant sessionArg =
      QVariant::fromValue(QDBusObjectPath(session.handle));

    // 2. SelectSources: monitors, all of them (one permission dialog, once)
    {
        QVariantMap opts{
            { QStringLiteral("types"), QVariant::fromValue<uint>(1) },
            { QStringLiteral("multiple"), true },
            { QStringLiteral("cursor_mode"),
              QVariant::fromValue<uint>(options.embedCursor ? 2 : 1) },
            { QStringLiteral("persist_mode"), QVariant::fromValue<uint>(2) },
        };
        if (!options.restoreToken.isEmpty()) {
            opts.insert(QStringLiteral("restore_token"), options.restoreToken);
        }
        const PortalResponse resp = callRequest(QStringLiteral("SelectSources"),
                                                { sessionArg },
                                                opts,
                                                options.portalTimeoutMs);
        if (!resp.ok()) {
            r.error = resp.error;
            return r;
        }
    }

    // 3. Start (shows the permission dialog when there is no valid token)
    QList<ScreenCastStream> streams;
    {
        const int timeout = options.restoreToken.isEmpty()
                              ? options.interactiveTimeoutMs
                              : options.portalTimeoutMs;
        const PortalResponse resp =
          callRequest(QStringLiteral("Start"),
                      { sessionArg, options.parentWindow },
                      {},
                      timeout);
        // The token is single use: keep the new one even if we fail later.
        r.restoreToken =
          resp.results.value(QStringLiteral("restore_token")).toString();
        if (!resp.ok()) {
            r.error = resp.error;
            return r;
        }
        streams = parseStreams(resp.results.value(QStringLiteral("streams")));
        if (streams.isEmpty()) {
            r.error = tr("Start returned no monitor streams");
            return r;
        }
    }

    // 4. Which stream is which screen
    const QList<int> match = matchStreamsToScreens(streams, screenGeometries);
    PwCapture capture;
    for (int idx : wanted) {
        if (match.at(idx) < 0) {
            r.error = tr("No ScreenCast stream matches screen %1 (%2x%3+%4+%5)."
                         " The remembered permission may not include every "
                         "monitor; it will be asked again next time.")
                        .arg(idx)
                        .arg(screenGeometries.at(idx).width())
                        .arg(screenGeometries.at(idx).height())
                        .arg(screenGeometries.at(idx).x())
                        .arg(screenGeometries.at(idx).y());
            r.restoreToken = QString();
            r.forgetToken = true;
            return r;
        }
        capture.addStream(idx, streams.at(match.at(idx)).nodeId);
    }

    // 5. PipeWire remote
    int pwFd = -1;
    {
        QDBusMessage open = QDBusMessage::createMethodCall(
          kService, kPath, kScreenCast, QStringLiteral("OpenPipeWireRemote"));
        open.setArguments({ sessionArg, QVariantMap() });
        const QDBusMessage reply =
          bus.call(open, QDBus::Block, options.portalTimeoutMs);
        if (reply.type() == QDBusMessage::ErrorMessage ||
            reply.arguments().isEmpty()) {
            r.error =
              tr("OpenPipeWireRemote failed: %1").arg(reply.errorMessage());
            return r;
        }
        const auto fd =
          reply.arguments().first().value<QDBusUnixFileDescriptor>();
        if (!fd.isValid()) {
            r.error = tr("OpenPipeWireRemote returned no file descriptor");
            return r;
        }
        pwFd = fcntl(fd.fileDescriptor(), F_DUPFD_CLOEXEC, 3);
        if (pwFd < 0) {
            r.error = tr("Could not duplicate the PipeWire file descriptor");
            return r;
        }
    }

    // 6. First frame of each wanted stream
    const QString runError = capture.run(pwFd, options.frameTimeoutMs);
    session.close(); // frames are in memory; end sharing right away
    if (!runError.isEmpty()) {
        r.error = runError;
        return r;
    }
    for (const auto& slot : capture.streamSlots()) {
        if (!slot->error.isEmpty() || slot->image.isNull()) {
            r.error =
              slot->error.isEmpty() ? tr("No frame received") : slot->error;
            r.images.clear();
            return r;
        }
        QImage img = slot->image;
        const int logicalW = screenGeometries.at(slot->screenIndex).width();
        img.setDevicePixelRatio(qreal(img.width()) / qreal(logicalW));
        r.images.insert(slot->screenIndex, img);
    }
    return r;
}

#endif // FLAMESHOT_HAVE_PIPEWIRE
