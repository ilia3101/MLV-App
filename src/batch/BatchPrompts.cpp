#include "BatchPrompts.h"
#include "BatchContext.h"

#include <QTextStream>
#include <QMessageBox>
#include <QApplication>

/* Static member definition */
std::function<void()> BatchPrompts::s_abortBatchFn;

void BatchPrompts::setAbortBatchCallback(std::function<void()> fn)
{
    s_abortBatchFn = fn;
}

bool BatchPrompts::shouldSkipFrame(const QString &clipName,
                                   int frameIndex,
                                   const QString &errorDetail)
{
    if( BatchContext::isBatchMode() )
    {
        QTextStream err(stderr);
        if( BatchContext::skipErrors() )
        {
            err << QStringLiteral("[BATCH] SKIP %1 frame=%2 error=%3\n")
                       .arg( clipName ).arg( frameIndex ).arg( errorDetail );
            err.flush();
            return true; /* skip frame, continue */
        }
        else
        {
            err << QStringLiteral("[BATCH] ERROR %1 frame=%2 error=%3\n")
                       .arg( clipName ).arg( frameIndex ).arg( errorDetail );
            err.flush();
            return false; /* abort */
        }
    }

    /* GUI mode — show the original 3-button QMessageBox */
    QWidget *parent = QApplication::activeWindow();
    int ret = QMessageBox::critical(
        parent,
        QObject::tr( "MLV App - Export file error" ),
        QObject::tr( "Could not save: %1\nHow do you like to proceed?" ).arg( errorDetail ),
        QObject::tr( "Skip frame" ),
        QObject::tr( "Abort current export" ),
        QObject::tr( "Abort batch export" ),
        0, 2 );

    if( ret == 2 )
    {
        /* "Abort batch export" — invoke the callback if set */
        if( s_abortBatchFn ) s_abortBatchFn();
        return false;
    }
    if( ret == 1 )
    {
        /* "Abort current export" */
        return false;
    }
    /* ret == 0: "Skip frame" */
    return true;
}

bool BatchPrompts::shouldContinue(const QString &context,
                                  const QString &message)
{
    if( BatchContext::isBatchMode() )
    {
        QTextStream err(stderr);
        err << QStringLiteral("[BATCH] WARNING %1: %2\n").arg( context, message );
        err.flush();
        /* In batch mode, disk-full is always fatal */
        return false;
    }

    /* GUI mode — show warning dialog with context and message */
    QWidget *parent = QApplication::activeWindow();
    QMessageBox::warning( parent,
        QStringLiteral("MLV App"),
        QStringLiteral("%1: %2").arg( context, message ) );
    return false; /* always abort on disk full */
}
