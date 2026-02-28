#include "BatchRunner.h"
#include "BatchContext.h"

#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QElapsedTimer>

/* MainWindow.h gives us the static exportCdngSequence helper
 * and pulls in mlv_include.h (C API) transitively. */
#include "../../platform/qt/MainWindow.h"
#include "../../platform/qt/ExportSettingsDialog.h"
#include "../../platform/qt/StretchFactors.h"

int BatchRunner::run(const QString &inputPath, const QString &outputPath)
{
    QTextStream out(stdout);
    QElapsedTimer totalTimer;
    totalTimer.start();

    /* Collect list of .mlv files to process */
    QStringList mlvFiles;
    QFileInfo inputInfo(inputPath);

    if( inputInfo.isFile() )
    {
        if( inputPath.endsWith( QStringLiteral(".mlv"), Qt::CaseInsensitive ) )
            mlvFiles << inputPath;
        else
        {
            out << QStringLiteral("[BATCH] ERROR: Input is not an MLV file: %1\n").arg(inputPath);
            out.flush();
            return 3;
        }
    }
    else if( inputInfo.isDir() )
    {
        QDir dir(inputPath);
        QStringList filters;
        filters << QStringLiteral("*.mlv") << QStringLiteral("*.MLV");
        QFileInfoList entries = dir.entryInfoList( filters, QDir::Files, QDir::Name );
        for( const QFileInfo &fi : entries )
            mlvFiles << fi.absoluteFilePath();

        if( mlvFiles.isEmpty() )
        {
            out << QStringLiteral("[BATCH] ERROR: No MLV files found in: %1\n").arg(inputPath);
            out.flush();
            return 3;
        }
    }
    else
    {
        out << QStringLiteral("[BATCH] ERROR: Input path does not exist: %1\n").arg(inputPath);
        out.flush();
        return 3;
    }

    /* Ensure output directory exists */
    QDir outDir(outputPath);
    if( !outDir.exists() )
    {
        if( !outDir.mkpath( QStringLiteral(".") ) )
        {
            out << QStringLiteral("[BATCH] ERROR: Cannot create output directory: %1\n").arg(outputPath);
            out.flush();
            return 3;
        }
    }

    out << QStringLiteral("[BATCH] START input=%1 output=%2 skip-errors=%3\n")
               .arg( inputPath, outputPath,
                     BatchContext::skipErrors() ? QStringLiteral("true")
                                               : QStringLiteral("false") );
    out.flush();

    int succeeded = 0;
    int failed = 0;

    for( const QString &mlvPath : mlvFiles )
    {
        ProcessResult res = exportSingleFile( mlvPath, outputPath );

        QString baseName = QFileInfo(mlvPath).completeBaseName();
        if( res.success )
        {
            out << QStringLiteral("[BATCH] DONE %1 exported=%2 skipped=%3 elapsed=%4\n")
                       .arg( baseName )
                       .arg( res.framesExported )
                       .arg( res.framesSkipped )
                       .arg( res.elapsedSeconds, 0, 'f', 1 );
            out.flush();
            succeeded++;
        }
        else
        {
            out << QStringLiteral("[BATCH] FAIL %1 error=%2 exported=%3 skipped=%4 elapsed=%5\n")
                       .arg( baseName,
                              res.errorMessage )
                       .arg( res.framesExported )
                       .arg( res.framesSkipped )
                       .arg( res.elapsedSeconds, 0, 'f', 1 );
            out.flush();
            failed++;

            /* Without --skip-errors, abort the entire batch on first failure */
            if( !BatchContext::skipErrors() )
            {
                out << QStringLiteral("[BATCH] COMPLETE files=%1 succeeded=%2 failed=%3 total_elapsed=%4\n")
                           .arg( mlvFiles.size() ).arg( succeeded ).arg( failed )
                           .arg( totalTimer.elapsed() / 1000.0, 0, 'f', 1 );
                out.flush();
                return 4;
            }
        }
    }

    double totalElapsed = totalTimer.elapsed() / 1000.0;
    out << QStringLiteral("[BATCH] COMPLETE files=%1 succeeded=%2 failed=%3 total_elapsed=%4\n")
               .arg( mlvFiles.size() ).arg( succeeded ).arg( failed )
               .arg( totalElapsed, 0, 'f', 1 );
    out.flush();

    if( failed > 0 ) return 1;
    return 0;
}

ProcessResult BatchRunner::exportSingleFile(const QString &mlvPath,
                                            const QString &outputRoot)
{
    ProcessResult result;
    QTextStream out(stdout);
    QString baseName = QFileInfo(mlvPath).completeBaseName();

    /* Open MLV file using the C API — same as MainWindow::openMlv() */
    int mlvErr = MLV_ERR_NONE;
    char mlvErrMsg[256] = { 0 };

#ifdef Q_OS_UNIX
    mlvObject_t *mlvObject = initMlvObjectWithClip(
        mlvPath.toUtf8().data(), MLV_OPEN_FULL, &mlvErr, mlvErrMsg );
#else
    mlvObject_t *mlvObject = initMlvObjectWithClip(
        mlvPath.toLatin1().data(), MLV_OPEN_FULL, &mlvErr, mlvErrMsg );
#endif

    if( mlvErr )
    {
        result.success = false;
        result.errorMessage = QStringLiteral("Cannot open MLV: %1").arg( QString(mlvErrMsg) );
        if( mlvObject ) freeMlvObject( mlvObject );
        return result;
    }

    /* Create processing object with default settings */
    processingObject_t *processingObject = initProcessingObject();
    setMlvProcessing( mlvObject, processingObject );
    disableMlvCaching( mlvObject );
    setMlvCpuCores( mlvObject, QThread::idealThreadCount() );

    uint32_t totalFrames = getMlvFrames( mlvObject );
    out << QStringLiteral("[BATCH] FILE %1 frames=%2\n").arg( baseName ).arg( totalFrames );
    out.flush();

    /* Export using defaults:
     * - codecProfile = CODEC_CDNG (uncompressed)
     * - codecOption = CODEC_CNDG_DEFAULT (standard naming)
     * - cutIn = 1, cutOut = totalFrames (all frames)
     * - stretchX/Y = 1.0 (no stretch)
     * - audioExport = true, rawFixEnabled = true */
    result = MainWindow::exportCdngSequence(
        mlvObject,
        outputRoot,
        baseName,
        CODEC_CDNG,           /* uncompressed CDNG */
        CODEC_CNDG_DEFAULT,   /* standard folder/file naming */
        1,                    /* cutIn: first frame */
        totalFrames,          /* cutOut: last frame */
        STRETCH_H_100,        /* no horizontal stretch */
        STRETCH_V_100,        /* no vertical stretch */
        true,                 /* export audio if present */
        true                  /* enable raw fixes */
    );

    /* Clean up */
    freeMlvObject( mlvObject );
    freeProcessingObject( processingObject );

    return result;
}
