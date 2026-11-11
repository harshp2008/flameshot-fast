// Pure-logic tests for the ScreenCast grabber: no session bus, no PipeWire.
#include "colornames.h"
#include "screencastgrabber.h"

#include <QtTest>

class TstPure : public QObject
{
    Q_OBJECT
private slots:
    void matchExact();
    void matchPhoneAndTabletLayout();
    void matchReorderedStreams();
    void matchToleranceForFractionalScale();
    void matchRejectsFarAndAmbiguous();
    void matchNeverSharesStream();
    void pointerParsing();
    void monitorAtPoint();
    void bufferBgrx();
    void bufferRgbx();
    void bufferBgraAndRgba();
    void bufferStridePadding();
    void bufferRejectsBadInput();
    void colorFormats();
    void colorNames();
    void colorShades();
    void colorHistory();
};

static ScreenCastStream S(quint32 id, int x, int y, int w, int h)
{
    return { id, QPoint(x, y), QSize(w, h) };
}

void TstPure::matchExact()
{
    const QList<ScreenCastStream> streams = { S(40, 0, 0, 1920, 1080) };
    const QList<QRect> screens = { QRect(0, 0, 1920, 1080) };
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(streams, screens),
             QList<int>({ 0 }));
}

// The layout measured on the target machine (see FINDINGS.md)
void TstPure::matchPhoneAndTabletLayout()
{
    const QList<ScreenCastStream> streams = { S(51, 2856, 0, 1464, 915),
                                              S(49, 0, 0, 1920, 1080),
                                              S(50, 1920, 0, 936, 432) };
    const QList<QRect> screens = { QRect(0, 0, 1920, 1080),
                                   QRect(1920, 0, 936, 432),
                                   QRect(2856, 0, 1464, 915) };
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(streams, screens),
             QList<int>({ 1, 2, 0 }));
}

void TstPure::matchReorderedStreams()
{
    const QList<ScreenCastStream> streams = { S(2, 1920, 0, 1280, 720),
                                              S(1, 0, 0, 1920, 1080) };
    const QList<QRect> screens = { QRect(1920, 0, 1280, 720),
                                   QRect(0, 0, 1920, 1080) };
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(streams, screens),
             QList<int>({ 0, 1 }));
}

void TstPure::matchToleranceForFractionalScale()
{
    // 2560 / 1.7486 = 1464.0..; compositors may round by a pixel
    const QList<ScreenCastStream> streams = { S(7, 2856, 0, 1465, 915) };
    const QList<QRect> screens = { QRect(2856, 0, 1464, 915) };
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(streams, screens),
             QList<int>({ 0 }));
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(streams, screens, 0),
             QList<int>({ -1 }));
}

void TstPure::matchRejectsFarAndAmbiguous()
{
    // wrong size
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(
               { S(1, 0, 0, 1280, 720) }, { QRect(0, 0, 1920, 1080) }),
             QList<int>({ -1 }));
    // wrong position
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(
               { S(1, 100, 0, 1920, 1080) }, { QRect(0, 0, 1920, 1080) }),
             QList<int>({ -1 }));
    // two nearly identical candidates and neither is exact
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(
               { S(1, 0, 1, 1920, 1080), S(2, 1, 0, 1920, 1080) },
               { QRect(0, 0, 1920, 1080) }),
             QList<int>({ -1 }));
    // exact beats near
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(
               { S(1, 1, 0, 1920, 1080), S(2, 0, 0, 1920, 1080) },
               { QRect(0, 0, 1920, 1080) }),
             QList<int>({ 1 }));
    // no streams
    QCOMPARE(
      ScreenCastGrabber::matchStreamsToScreens({}, { QRect(0, 0, 10, 10) }),
      QList<int>({ -1 }));
}

void TstPure::matchNeverSharesStream()
{
    // Mirrored outputs: two screens, one stream
    QCOMPARE(ScreenCastGrabber::matchStreamsToScreens(
               { S(1, 0, 0, 1920, 1080) },
               { QRect(0, 0, 1920, 1080), QRect(0, 0, 1920, 1080) }),
             QList<int>({ -1, -1 }));
}

void TstPure::pointerParsing()
{
    QPoint p;
    QVERIFY(ScreenCastGrabber::parsePointer("1154, 488\n", p));
    QCOMPARE(p, QPoint(1154, 488));
    QVERIFY(ScreenCastGrabber::parsePointer("  -20 30", p));
    QCOMPARE(p, QPoint(-20, 30));
    QVERIFY(ScreenCastGrabber::parsePointer("x=5,y=6", p) == false);
    QVERIFY(!ScreenCastGrabber::parsePointer("", p));
    QVERIFY(!ScreenCastGrabber::parsePointer("error: no shell", p));
}

void TstPure::monitorAtPoint()
{
    const QList<QRect> screens = { QRect(0, 0, 1920, 1080),
                                   QRect(1920, 0, 1464, 915),
                                   QRect(3384, 0, 936, 432) };
    QCOMPARE(ScreenCastGrabber::monitorAt(screens, QPoint(1154, 488)), 0);
    QCOMPARE(ScreenCastGrabber::monitorAt(screens, QPoint(1920, 0)), 1);
    QCOMPARE(ScreenCastGrabber::monitorAt(screens, QPoint(3383, 914)), 1);
    QCOMPARE(ScreenCastGrabber::monitorAt(screens, QPoint(4000, 100)), 2);
    QCOMPARE(ScreenCastGrabber::monitorAt(screens, QPoint(4000, 800)), -1);
}

void TstPure::bufferBgrx()
{
    // memory order B, G, R, x
    const uchar px[] = { 10, 20, 30, 0, 40, 50, 60, 0 };
    const QImage img = ScreenCastGrabber::imageFromBuffer(
      ScreenCastPixelFormat::BGRx, QSize(2, 1), 8, px, sizeof(px));
    QVERIFY(!img.isNull());
    QCOMPARE(img.size(), QSize(2, 1));
    QCOMPARE(img.pixelColor(0, 0).red(), 30);
    QCOMPARE(img.pixelColor(0, 0).green(), 20);
    QCOMPARE(img.pixelColor(0, 0).blue(), 10);
    QCOMPARE(img.pixelColor(1, 0).red(), 60);
    // the unused byte must not make the pixel transparent
    QCOMPARE(img.pixelColor(0, 0).alpha(), 255);
}

void TstPure::bufferRgbx()
{
    const uchar px[] = { 30, 20, 10, 0 };
    const QImage img = ScreenCastGrabber::imageFromBuffer(
      ScreenCastPixelFormat::RGBx, QSize(1, 1), 4, px, sizeof(px));
    QVERIFY(!img.isNull());
    QCOMPARE(img.pixelColor(0, 0).red(), 30);
    QCOMPARE(img.pixelColor(0, 0).green(), 20);
    QCOMPARE(img.pixelColor(0, 0).blue(), 10);
    QCOMPARE(img.pixelColor(0, 0).alpha(), 255);
}

void TstPure::bufferBgraAndRgba()
{
    const uchar bgra[] = { 10, 20, 30, 255 };
    QImage a = ScreenCastGrabber::imageFromBuffer(
      ScreenCastPixelFormat::BGRA, QSize(1, 1), 4, bgra, 4);
    QVERIFY(!a.isNull());
    QCOMPARE(a.pixelColor(0, 0), QColor(30, 20, 10, 255));
    const uchar rgba[] = { 30, 20, 10, 255 };
    QImage b = ScreenCastGrabber::imageFromBuffer(
      ScreenCastPixelFormat::RGBA, QSize(1, 1), 4, rgba, 4);
    QVERIFY(!b.isNull());
    QCOMPARE(b.pixelColor(0, 0), QColor(30, 20, 10, 255));
}

void TstPure::bufferStridePadding()
{
    // 2x2 with a 12-byte stride (4 bytes of padding per row)
    const uchar px[] = { 1,  2,  3, 0, 4, 5, 6,  0,  99, 99,
                         99, 99, 7, 8, 9, 0, 10, 11, 12, 0 };
    const QImage img = ScreenCastGrabber::imageFromBuffer(
      ScreenCastPixelFormat::BGRx, QSize(2, 2), 12, px, sizeof(px));
    QVERIFY(!img.isNull());
    QCOMPARE(img.pixelColor(0, 1).blue(), 7);
    QCOMPARE(img.pixelColor(1, 1).red(), 12);
    QCOMPARE(img.pixelColor(1, 0).blue(), 4);
}

void TstPure::bufferRejectsBadInput()
{
    const uchar px[16] = {};
    using F = ScreenCastPixelFormat;
    QVERIFY(ScreenCastGrabber::imageFromBuffer(F::BGRx, QSize(2, 2), 8, px, 15)
              .isNull()); // too short
    QVERIFY(ScreenCastGrabber::imageFromBuffer(F::BGRx, QSize(4, 1), 8, px, 16)
              .isNull()); // stride < row
    QVERIFY(ScreenCastGrabber::imageFromBuffer(F::BGRx, QSize(0, 1), 4, px, 16)
              .isNull());
    QVERIFY(
      ScreenCastGrabber::imageFromBuffer(F::BGRx, QSize(1, 1), 4, nullptr, 4)
        .isNull());
    QVERIFY(ScreenCastGrabber::imageFromBuffer(
              F::BGRx, QSize(100000, 1), 400000, px, 16)
              .isNull());
}

// Values from Microsoft's Color Picker documentation (cbb6ac)
void TstPure::colorFormats()
{
    const QColor c(203, 182, 172);
    QCOMPARE(ColorNames::hex(c), QString("#cbb6ac"));
    QCOMPARE(ColorNames::rgb(c), QString("rgb(203, 182, 172)"));
    QCOMPARE(ColorNames::hsl(c), QString("hsl(19, 23%, 74%)"));
    QCOMPARE(ColorNames::hsv(c), QString("hsv(19, 15%, 80%)"));
    QCOMPARE(ColorNames::format(c, "RGB"), ColorNames::rgb(c));
    QCOMPARE(ColorNames::format(c, "nonsense"), ColorNames::hex(c));
    // grays have no hue: it must be printed as 0, not -1
    QCOMPARE(ColorNames::hsl(QColor(128, 128, 128)), QString("hsl(0, 0%, 50%)"));
    QCOMPARE(ColorNames::hsv(QColor(0, 0, 0)), QString("hsv(0, 0%, 0%)"));
    QCOMPARE(ColorNames::hsv(QColor(156, 182, 135)), QString("hsv(93, 26%, 71%)"));
}

void TstPure::colorNames()
{
    QCOMPARE(ColorNames::nameFor(QColor("#a8c7d2")), QString("Sky blue"));
    QCOMPARE(ColorNames::nameFor(QColor("#adcad4")), QString("Sky blue"));
    QCOMPARE(ColorNames::nameFor(QColor("#f0d9d5")), QString("Rose"));
    QCOMPARE(ColorNames::nameFor(QColor("#000000")), QString("Black"));
    QCOMPARE(ColorNames::nameFor(QColor("#ffffff")), QString("White"));
    QCOMPARE(ColorNames::nameFor(QColor("#ff0000")), QString("Red"));
}

void TstPure::colorShades()
{
    const QColor c("#e02020");
    const auto s = ColorNames::shades(c, 7);
    QCOMPARE(s.size(), 7);
    QCOMPARE(s[3], c);
    QVERIFY(s.first().lightnessF() > s.last().lightnessF());
}

void TstPure::colorHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/sub/hist.json";
    {
        ColorNames::History h(path);
        QVERIFY(h.colors().isEmpty());
        h.add(QColor("#111111"));
        h.add(QColor("#222222"));
        h.add(QColor("#111111")); // moves to the front, no duplicate
        QCOMPARE(h.colors(), QList<QColor>({ QColor("#111111"), QColor("#222222") }));
    }
    {
        ColorNames::History h(path); // persisted
        QCOMPARE(h.colors().size(), 2);
        h.remove(QColor("#111111"));
        QCOMPARE(h.colors(), QList<QColor>({ QColor("#222222") }));
        for (int i = 0; i < 40; ++i) {
            h.add(QColor::fromRgb(i, i, i));
        }
        QCOMPARE(h.colors().size(), 24);
        QCOMPARE(h.colors().first(), QColor::fromRgb(39, 39, 39));
    }
    QFile bad(path);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("not json at all");
    bad.close();
    ColorNames::History h(path); // corrupt file: starts empty, no crash
    QVERIFY(h.colors().isEmpty());
}

QTEST_APPLESS_MAIN(TstPure)
#include "tst_pure.moc"
