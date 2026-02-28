#include "BatchLogger.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

/* File-scope statics — no heap allocation needed.
 * s_logFile is opened/closed by init()/shutdown(). */
static QFile       s_logFile;
static QTextStream s_logStream;
static bool        s_logOpen = false;

void BatchLogger::init(const QString &logPath)
{
    if( logPath.isEmpty() ) return;

    /* Create parent directory if needed */
    QString dir = QFileInfo(logPath).absolutePath();
    QDir().mkpath(dir);

    s_logFile.setFileName(logPath);
    if( !s_logFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    {
        QTextStream e(stderr);
        e << QStringLiteral("[BATCH] WARNING: Cannot open log file: %1\n").arg(logPath);
        e.flush();
        return;
    }
    s_logStream.setDevice(&s_logFile);
    s_logOpen = true;
}

void BatchLogger::out(const QString &line)
{
    QTextStream o(stdout);
    o << line;
    o.flush();

    if( s_logOpen )
    {
        s_logStream << line;
        s_logStream.flush();
    }
}

void BatchLogger::err(const QString &line)
{
    QTextStream e(stderr);
    e << line;
    e.flush();

    if( s_logOpen )
    {
        s_logStream << line;
        s_logStream.flush();
    }
}

void BatchLogger::shutdown()
{
    if( s_logOpen )
    {
        s_logStream.flush();
        s_logStream.setDevice(nullptr);
        s_logFile.close();
        s_logOpen = false;
    }
}
