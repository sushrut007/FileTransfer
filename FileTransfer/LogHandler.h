#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QtMessageHandler>
#include <QMetaObject>

class LogHandler : public QObject
{
    Q_OBJECT
public:
    enum class Level { Debug, Info, Warning, Critical };

    static LogHandler* instance() {
        static LogHandler inst;
        return &inst;
    }

    void install() {
        qInstallMessageHandler(&LogHandler::messageHandler);
    }

signals:
    void logLine(LogHandler::Level level,
        const QString& timestamp,
        const QString& message);

private:
    LogHandler() = default;

    static void messageHandler(QtMsgType type,
        const QMessageLogContext&,
        const QString& msg)
    {
        Level level = Level::Debug;
        switch (type) {
        case QtDebugMsg:    level = Level::Debug;    break;
        case QtInfoMsg:     level = Level::Info;     break;
        case QtWarningMsg:  level = Level::Warning;  break;
        case QtCriticalMsg:
        case QtFatalMsg:    level = Level::Critical; break;
        }

        const QString ts = QDateTime::currentDateTime()
            .toString(QStringLiteral("hh:mm:ss.zzz"));

        // Thread-safe signal emission via queued connection
        QMetaObject::invokeMethod(
            instance(),
            [level, ts, msg]() {
                emit instance()->logLine(level, ts, msg);
            },
            Qt::QueuedConnection);
    }
};
Q_DECLARE_METATYPE(LogHandler::Level)