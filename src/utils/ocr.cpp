// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocr.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "utils/systemnotification.h"

#include <QBuffer>
#include <QProcess>
#include <QRegularExpression>

QString Ocr::tidy(const QString& raw)
{
    QString text = raw;
    text.remove(QLatin1Char('\f'));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    // trailing blanks per line, then leading/trailing empty lines
    text.replace(QRegularExpression(QStringLiteral("[ \\t]+\\n")),
                 QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral("^\\s*\\n")));
    text.remove(QRegularExpression(QStringLiteral("\\s+$")));
    return text;
}

QString Ocr::recognize(const QPixmap& image, QString& error)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");

    QProcess tesseract;
    tesseract.start(QStringLiteral("tesseract"),
                    { QStringLiteral("stdin"),
                      QStringLiteral("stdout"),
                      QStringLiteral("-l"),
                      ConfigHandler().ocrLanguage() });
    if (!tesseract.waitForStarted(3000)) {
        error = QObject::tr("Could not start `tesseract` (is "
                            "tesseract-ocr installed?)");
        return QString();
    }
    tesseract.write(png);
    tesseract.closeWriteChannel();
    if (!tesseract.waitForFinished(30000) ||
        tesseract.exitStatus() != QProcess::NormalExit ||
        tesseract.exitCode() != 0) {
        error =
          QObject::tr("tesseract failed: %1")
            .arg(QString::fromUtf8(tesseract.readAllStandardError()).trimmed());
        tesseract.kill();
        return QString();
    }
    return tidy(QString::fromUtf8(tesseract.readAllStandardOutput()));
}

QString Ocr::recognizeWithNotification(const QPixmap& image)
{
    QString error;
    const QString text = recognize(image, error);
    SystemNotification notification;
    if (!error.isEmpty()) {
        AbstractLogger::error() << error;
        return QString();
    }
    if (text.isEmpty()) {
        notification.sendMessage(QObject::tr("No text found in the selection."),
                                 QObject::tr("Nothing copied"),
                                 QString(),
                                 4000);
        AbstractLogger::info(AbstractLogger::Stderr | AbstractLogger::LogFile)
          << QObject::tr("OCR found no text");
        return QString();
    }
    QString preview = text;
    if (preview.size() > 300) {
        preview = preview.left(300) + QStringLiteral("...");
    }
    notification.sendMessage(
      preview, QObject::tr("Text Copied!"), QString(), 6000);
    AbstractLogger::info(AbstractLogger::Stderr | AbstractLogger::LogFile)
      << QObject::tr("OCR text copied to clipboard (%1 characters)")
           .arg(text.size());
    return text;
}
