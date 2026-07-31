#include "infrastructure/local_logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageLogContext>
#include <QMutex>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

#include <cstdio>
#include <memory>

namespace rawviewer::infrastructure {
namespace {

QMutex logMutex;
std::unique_ptr<QFile> logFile;

const char* levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "unknown";
}

void messageHandler(QtMsgType type,
                    const QMessageLogContext&,
                    const QString& message) {
    const QString line = QStringLiteral("%1 level=%2 message=\"%3\"\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             QString::fromLatin1(levelName(type)),
             QString(message).replace('"', '\'').replace('\n', ' '));

    QMutexLocker lock(&logMutex);
    if (logFile && logFile->isOpen()) {
        logFile->write(line.toUtf8());
        logFile->flush();
    }
    std::fputs(line.toLocal8Bit().constData(), stderr);
    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

void installLocalLogging() {
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!directory.isEmpty() && QDir().mkpath(directory)) {
        logFile = std::make_unique<QFile>(directory + "/raw-viewer.log");
        logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    qInstallMessageHandler(messageHandler);
}

} // namespace rawviewer::infrastructure
