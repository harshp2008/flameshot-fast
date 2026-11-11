// Writes an 800x800 PNG with known text for the OCR test: make_text_png OUT.png
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QImage img(800, 800, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(Qt::black);
    QFont f(QStringLiteral("DejaVu Sans"));
    f.setPixelSize(44);
    p.setFont(f);
    p.drawText(40, 120, QStringLiteral("Hello flameshot OCR"));
    p.drawText(40, 200, QStringLiteral("second line 12345"));
    p.drawText(40, 360, QStringLiteral("after a gap"));
    p.end();
    return img.save(QString::fromLocal8Bit(argc > 1 ? argv[1] : "out.png")) ? 0
                                                                            : 1;
}
