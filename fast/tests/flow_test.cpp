// Drives ScreenCastGrabber::grab() against whatever portal is on the session
// bus (the mock in mock_portal.py, or none) and checks the outcome.
//
//   flow_test EXPECT            EXPECT = OK | text the error must contain
//
// Env: FLOW_SCREENS  "x,y,w,h;x,y,w,h" logical screen geometries
//      FLOW_TOKEN    restore token to pass in (default none)
//      FLOW_WANT     comma list of screen indices (default: all)
//      FLOW_EXPECT_PIXEL  "idx:x,y=r,g,b" check one pixel (OK mode only)
//      FLOW_EXPECT_DPR    "idx=value"
#include "screencastgrabber.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QString expect = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "OK";

    QList<QRect> screens;
    const QString spec =
      qEnvironmentVariable("FLOW_SCREENS", QStringLiteral("0,0,800,600"));
    for (const QString& s : spec.split(';', Qt::SkipEmptyParts)) {
        const QStringList v = s.split(',');
        screens.append(
          QRect(v[0].toInt(), v[1].toInt(), v[2].toInt(), v[3].toInt()));
    }
    QList<int> wanted;
    if (qEnvironmentVariableIsSet("FLOW_WANT")) {
        for (const QString& s : qEnvironmentVariable("FLOW_WANT").split(','))
            wanted.append(s.toInt());
    } else {
        for (int i = 0; i < screens.size(); ++i)
            wanted.append(i);
    }

    ScreenCastGrabber::Options opt;
    opt.restoreToken = qEnvironmentVariable("FLOW_TOKEN");
    opt.portalTimeoutMs = 1500;
    opt.interactiveTimeoutMs = 1500; // tests must not wait for a human
    opt.frameTimeoutMs = 1500;

    QElapsedTimer t;
    t.start();
    const ScreenCastGrabber::Result r =
      ScreenCastGrabber().grab(screens, wanted, opt);
    out << "elapsed_ms=" << t.elapsed() << "\n";
    out << "error=" << r.error << "\n";
    out << "token=" << r.restoreToken << "\n";
    out << "forget=" << (r.forgetToken ? 1 : 0) << "\n";
    for (auto it = r.images.cbegin(); it != r.images.cend(); ++it) {
        out << "image " << it.key() << " " << it.value().width() << "x"
            << it.value().height() << " dpr=" << it.value().devicePixelRatio()
            << "\n";
    }
    out.flush();

    if (expect == QLatin1String("OK")) {
        if (!r.ok())
            return 1;
        if (qEnvironmentVariableIsSet("FLOW_EXPECT_PIXEL")) {
            // idx:x,y=r,g,b
            const QString p = qEnvironmentVariable("FLOW_EXPECT_PIXEL");
            const int idx = p.section(':', 0, 0).toInt();
            const QStringList xy =
              p.section(':', 1).section('=', 0, 0).split(',');
            const QStringList rgb = p.section('=', 1).split(',');
            const QColor c =
              r.images.value(idx).pixelColor(xy[0].toInt(), xy[1].toInt());
            out << "pixel=" << c.red() << "," << c.green() << "," << c.blue()
                << "\n";
            out.flush();
            if (c != QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt()))
                return 2;
        }
        if (qEnvironmentVariableIsSet("FLOW_EXPECT_DPR")) {
            const QString d = qEnvironmentVariable("FLOW_EXPECT_DPR");
            if (qAbs(r.images.value(d.section('=', 0, 0).toInt())
                       .devicePixelRatio() -
                     d.section('=', 1).toDouble()) > 1e-6)
                return 3;
        }
        return 0;
    }
    return r.error.contains(expect) ? 0 : 1;
}
