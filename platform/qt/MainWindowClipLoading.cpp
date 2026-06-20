/*!
 * \file MainWindowClipLoading.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The main window
 */

#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "math.h"

#include <QMessageBox>
#include <QThread>
#include <QTime>
#include <QSettings>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QDesktopWidget>
#else
#include <QWidget>
#endif
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QMimeData>
#include <QDir>
#include <QSpacerItem>
#include <QDate>
#include <QStorageInfo>
#include <QColorDialog>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef Q_OS_MACX
#include "AvfLibWrapper.h"
#include "MainWindow.h"
#endif

#include "ExportSettingsDialog.h"
#include "EditSliderValueDialog.h"
#include "DarkStyle.h"
#include "DarkStyleModern.h"
#include "Updater/updaterUI/cupdaterdialog.h"
#include "Updater/Updater.h"
#include "FcpxmlAssistantDialog.h"
#include "FcpxmlSelectDialog.h"
#include "UserManualDialog.h"
#include "StretchFactors.h"
#include "SingleFrameExportDialog.h"
#include "FpmInstaller.h"
#include "ScopesLabel.h"
#include "avir/avirthreadpool.h"
#include "MoveToTrash.h"
#include "OverwriteListDialog.h"
#include "PixelMapListDialog.h"
#include "TranscodeDialog.h"
#include "BadPixelFileHandler.h"
#include "FocusPixelMapManager.h"
#include "StatusFpmDialog.h"
#include "RenameDialog.h"

/* spaceTag argument options: ffmpeg color space tag number compliant */
#define SPACETAG_REC709   1   /* rec709 color space */
#define SPACETAG_UNKNOWN  2   /* No color space tag set */

#ifdef __cplusplus
extern "C" {
#endif

#include <../../src/mlv/camid/camera_id.h>
extern const char* camidGetCameraName(uint32_t cameraModel, int camname_type);

#ifdef __cplusplus
}
#endif

#define APPNAME "MLV App"
#define VERSION QString("%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR)
#define GITVERSION QString("QTv%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR)

#define FACTOR_DS       22.5
#define FACTOR_LS       11.2
#define FACTOR_LIGHTEN  0.6

#define ACTIVE_RECEIPT               m_pModel->receipt(m_pModel->activeRow())
#define GET_RECEIPT(index)           m_pModel->receipt(index)
#define ACTIVE_CLIP                  m_pModel->activeClip()
#define GET_CLIP(index)              m_pModel->clip(index)
#define SESSION_CLIP_COUNT           m_pModel->rowCount(QModelIndex())
#define SESSION_LAST_CLIP            m_pModel->receipt( SESSION_CLIP_COUNT - 1 )
#define SESSION_ACTIVE_CLIP_ROW      m_pModel->activeRow()
#define SET_ACTIVE_CLIP_IDX(index)   m_pModel->setActiveRow(index)
#define SESSION_EMPTY                m_pModel->rowCount(QModelIndex())==0

//Open a couple of MLVs
void MainWindow::openMlvSet( QStringList list )
{
    m_inOpeningProcess = true;
    for( int i = 0; i < list.size(); i++ )
    {
        QString fileName = list.at(i);
#ifdef Q_OS_WIN //Qt Bug?
        if( fileName.startsWith( "/" ) ) fileName.remove( 0, 1 );
#endif

        if( i == 0 && QFile(fileName).exists() && fileName.endsWith( ".command", Qt::CaseInsensitive ) )
        {
            if( m_pScripting->installScript( fileName ) )
                QMessageBox::information( this, APPNAME, tr( "Installation of script %1 successful." ).arg( QFileInfo( fileName ).fileName() ) );
            m_inOpeningProcess = false;
            return;
        }
        else if( i == 0 && QFile(fileName).exists() && fileName.endsWith( ".fpm", Qt::CaseInsensitive ) )
        {
            FpmInstaller::installFpm( &list );
            if( !list.empty() )
            {
                QString files;
                for( int i = 0; i < list.size(); i++ ) files.append( QString( "\r\n%1" ).arg( QFileInfo( list.at(i) ).fileName() ) );
                QMessageBox::information( this, APPNAME, tr( "Installation of focus pixel map(s) %1\r\nsuccessful." ).arg( files ) );
            }
            m_inOpeningProcess = false;
            return;
        }

        if( i == 0 && QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            if( SESSION_CLIP_COUNT && askToSaveCurrentSession() )
            {
                m_inOpeningProcess = false;
                return;
            }
            openSession( fileName );
        }
        else
        {
            //Exit if not an MLV file or aborted
            if( fileName == QString( "" ) || !(fileName.endsWith( ".mlv", Qt::CaseInsensitive ) || fileName.endsWith( ".mcraw", Qt::CaseInsensitive )) ) continue;
            importNewMlv( fileName );
        }
    }

    if( SESSION_CLIP_COUNT )
    {
        //Show last imported file
        showFileInEditor( SESSION_CLIP_COUNT - 1 );
    }

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Import a MLV, complete procedure
void MainWindow::importNewMlv(QString fileName)
{
    //File is already opened? Error!
    if( isFileInSession( fileName ) )
    {
        QMessageBox::information( this, tr( "Import MLV" ), tr( "File %1 already opened in session!" ).arg( fileName ) );
    }
    else
    {
        //Add to SessionList
        addFileToSession( fileName );

        //Open MLV
        int ret;
        if( ui->actionFastOpen->isChecked() ) ret = openMlvForPreview( fileName );
        else ret = openMlv( fileName );

        if( !ret )
        {
            //Save last file name
            m_lastMlvOpenFileName = fileName;

            on_actionResetReceipt_triggered();

            //Set to "please load when info is there"
            SESSION_LAST_CLIP->setFocusPixels( -1 );
            SESSION_LAST_CLIP->setStretchFactorY( -1 );

            previewPicture( SESSION_CLIP_COUNT - 1 );
        }
        else
        {
            //if open error, delete MLV
            deleteFileFromSession();
        }
    }
}

//Short open MLV function, call only for making a preview!
int MainWindow::openMlvForPreview(QString fileName)
{
    int mlvErr = MLV_ERR_NONE;
    char mlvErrMsg[256] = { 0 };

    mlvObject_t * new_MlvObject;

    if (fileName.endsWith( ".mcraw", Qt::CaseInsensitive))
    {
#ifdef Q_OS_UNIX
        new_MlvObject = initMlvObjectWithMcrawClip( fileName.toUtf8().data(), MLV_OPEN_PREVIEW, &mlvErr, mlvErrMsg );
#else
        new_MlvObject = initMlvObjectWithMcrawClip( fileName.toLatin1().data(), MLV_OPEN_PREVIEW, &mlvErr, mlvErrMsg );
#endif
        ui->comboBoxUseCameraMatrix->setCurrentIndex(0);
        on_comboBoxUseCameraMatrix_currentIndexChanged(0);
    }
    else
    {
#ifdef Q_OS_UNIX
        new_MlvObject = initMlvObjectWithClip( fileName.toUtf8().data(), MLV_OPEN_PREVIEW, &mlvErr, mlvErrMsg );
#else
        new_MlvObject = initMlvObjectWithClip( fileName.toLatin1().data(), MLV_OPEN_PREVIEW, &mlvErr, mlvErrMsg );
#endif
    }

    if( mlvErr )
    {
        QMessageBox::critical( this, tr( "MLV Error" ), tr( "%1" ).arg( mlvErrMsg ), QMessageBox::Cancel, QMessageBox::Cancel );
        freeMlvObject( new_MlvObject );
        return mlvErr;
    }

    //disable drawing and kill old timer and old WaveFormMonitor
    m_fileLoaded = false;
    m_dontDraw = true;

    //Waiting for thread being idle for not freeing used memory
    while( !m_pRenderThread->isIdle() ) {}
    //Waiting for frame ready because it works with m_pMlvObject
    while( m_frameStillDrawing ) {qApp->processEvents();}

    //Reset audio playback engine
    //m_pAudioPlayback->resetAudioEngine();

    /* Destroy it just for simplicity... and make a new one */
    freeMlvObject( m_pMlvObject );
    /* Set to NEW object with a NEW MLV clip! */
    m_pMlvObject = new_MlvObject;

    /* If use has terminal this is useful */
#ifndef STDOUT_SILENT
    printMlvInfo( m_pMlvObject );
#endif
    /* This needs to be joined (or segmentation fault 11 :D) */
    setMlvProcessing( m_pMlvObject, m_pProcessingObject );
    /* Disable Caching for the opening process */
    disableMlvCaching( m_pMlvObject );
    /* Limit frame cache to defined size of RAM */
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, m_cacheSizeMB );
    /* Tell it how many cores we have so it can be optimal */
    setMlvCpuCores( m_pMlvObject, QThread::idealThreadCount() );

    int imageSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
    if( m_pRawImage ) free( m_pRawImage );
    m_pRawImage = ( uint8_t* )malloc( imageSize );

    m_fileLoaded = true;

    //Raw black & white level (needed for preview picture)
    initRawBlackAndWhite();

    return MLV_ERR_NONE;
}

//Open MLV Dialog
void MainWindow::on_actionOpen_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastMlvOpenFileName ).absolutePath();
    if( !QDir( path ).exists() ) path = QDir::homePath();

    //Open File Dialog
    QStringList files = QFileDialog::getOpenFileNames( this, tr("Open one or more MLV..."),
                                                    path,
                                                    tr("Video (*.mlv *.MLV *.mcraw *.MCRAW)") );

    if( files.empty() ) return;

    m_inOpeningProcess = true;

    for( int i = 0; i < files.size(); i++ )
    {
        QString fileName = files.at(i);

        //Exit if not an MLV file or aborted
        if( fileName == QString( "" ) ||
            !(fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ||
              fileName.endsWith( ".mcraw", Qt::CaseInsensitive )) ) continue;

        importNewMlv( fileName );
    }

    //Show last imported file
    if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Import MLV files to session, which were used in FCPXML project
void MainWindow::on_actionFcpxmlImportAssistant_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //Get files from assistant dialog
    FcpxmlAssistantDialog *fcpAssi = new FcpxmlAssistantDialog( this );
    QStringList files;
    if( fcpAssi->exec() ) files = fcpAssi->getFileNames();
    else files.clear();
    delete fcpAssi;

    //No files or aborted? Do nothing...
    if( files.empty() ) return;

    //Open files
    m_inOpeningProcess = true;

    for( int i = 0; i < files.size(); i++ )
    {
        QString fileName = files.at(i);

        //Exit if not an MLV file or aborted
        if( fileName == QString( "" ) || !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) continue;

        importNewMlv( fileName );
    }

    //Show last imported file
    if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Open an assistant, which helps selection clips in session in dependency to clips which were used in FCPXML project
void MainWindow::on_actionFcpxmlSelectionAssistant_triggered()
{
    FcpxmlSelectDialog *sd = new FcpxmlSelectDialog( this, m_pModel, m_pProxyModel, m_pSelectionModel );
    sd->exec();
    delete sd;
}

//Open MLV procedure
int MainWindow::openMlv( QString fileName )
{
    //Select open mode
    int mlvOpenMode;
    if( ui->actionCreateMappFiles->isChecked() ) mlvOpenMode = MLV_OPEN_MAPP;
    else mlvOpenMode = MLV_OPEN_FULL;

    int mlvErr = MLV_ERR_NONE;
    char mlvErrMsg[256] = { 0 };

    mlvObject_t * new_MlvObject;

    if (fileName.endsWith( ".mcraw", Qt::CaseInsensitive))
    {
#ifdef Q_OS_UNIX
        new_MlvObject = initMlvObjectWithMcrawClip( fileName.toUtf8().data(), mlvOpenMode, &mlvErr, mlvErrMsg );
#else
        new_MlvObject = initMlvObjectWithMcrawClip( fileName.toLatin1().data(), mlvOpenMode, &mlvErr, mlvErrMsg );
#endif
    }
    else
    {
#ifdef Q_OS_UNIX
        new_MlvObject = initMlvObjectWithClip( fileName.toUtf8().data(), mlvOpenMode, &mlvErr, mlvErrMsg );
#else
        new_MlvObject = initMlvObjectWithClip( fileName.toLatin1().data(), mlvOpenMode, &mlvErr, mlvErrMsg );
#endif
    }

    if( mlvErr )
    {
        QMessageBox::critical( this, tr( "MLV Error" ), tr( "%1" ).arg( mlvErrMsg ), QMessageBox::Cancel, QMessageBox::Cancel );
        freeMlvObject( new_MlvObject );
        return mlvErr;
    }

    //Set window title to filename
    this->setWindowTitle( QString( "MLV App | %1" ).arg( fileName ) );

    m_fileLoaded = false;

    //Disable drawing and kill old timer and old WaveFormMonitor
    killTimer( m_timerId );
    m_dontDraw = true;

    //Waiting for thread being idle for not freeing used memory
    while( !m_pRenderThread->isIdle() ) {}
    //Waiting for frame ready because it works with m_pMlvObject
    while( m_frameStillDrawing ) {qApp->processEvents();}

    //Reset audio engine
    m_pAudioPlayback->resetAudioEngine();

    /* Destroy it just for simplicity... and make a new one */
    freeMlvObject( m_pMlvObject );
    /* Set to NEW object with a NEW MLV clip! */
    m_pMlvObject = new_MlvObject;

    /* If use has terminal this is useful */
#ifndef STDOUT_SILENT
    printMlvInfo( m_pMlvObject );
#endif
    /* This needs to be joined (or segmentation fault 11 :D) */
    setMlvProcessing( m_pMlvObject, m_pProcessingObject );
    /* Disable Caching for the opening process */
    disableMlvCaching( m_pMlvObject );
    /* Limit frame cache to defined size of RAM */
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, m_cacheSizeMB );
    /* Tell it how many cores we have so it can be optimal */
    setMlvCpuCores( m_pMlvObject, QThread::idealThreadCount() );

    //Adapt the RawImage to actual size
    int imageSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
    if( m_pRawImage ) free( m_pRawImage );
    m_pRawImage = ( uint8_t* )malloc( imageSize );

    //Init Render Thread
    m_pRenderThread->init( m_pMlvObject, m_pRawImage );

    //Calculate shutter flavors :)
    float shutterSpeed = 1000000.0f / (float)(getMlvShutter( m_pMlvObject ));
    float shutterAngle = getMlvFramerate( m_pMlvObject ) * 360.0f / shutterSpeed;

    //Form ISO info string.
    QString isoInfo = QString( "%1" ).arg( (int)getMlvIso( m_pMlvObject ) );
    QString dualIso = QString( "-" );
    QString dualIsoInfo = isoInfo;

    if( llrpGetDualIsoValidity( m_pMlvObject ) == DISO_VALID )
    {
        isoInfo = QString( "%1/%2" ).arg( m_pMlvObject->llrawproc->diso1 ).arg( m_pMlvObject->llrawproc->diso2 );
        dualIso = QString( "Dual ISO" );
        dualIsoInfo = QString( "%1, %2" ).arg( isoInfo ).arg( dualIso );
    }

    QString audioText;
    if( doesMlvHaveAudio( m_pMlvObject ) )
    {
        audioText = QString( "%1 channel(s),  %2 kHz" )
                .arg( getMlvAudioChannels( m_pMlvObject ) )
                .arg( getMlvSampleRate( m_pMlvObject ) );
    }
    else
    {
        audioText = QString( "-" );
    }

    ACTIVE_CLIP->updateMetadata( QString( "%1" ).arg( (char*)getMlvCamera( m_pMlvObject ) ),
                                 QString( "%1" ).arg( (char*)getMlvLens( m_pMlvObject ) ),
                                 QString( "%1 x %2 pixels" ).arg( (int)getMlvWidth( m_pMlvObject ) ).arg( (int)getMlvHeight( m_pMlvObject ) ),
                                 QString( "%1" ).arg( m_pTimeCodeImage->getTimeCodeFromFps( (int)getMlvFrames( m_pMlvObject ), getMlvFramerate( m_pMlvObject ) ) ),
                                 QString( "%1" ).arg( (int)getMlvFrames( m_pMlvObject ) ),
                                 QString( "%1 fps" ).arg( getMlvFramerate( m_pMlvObject ) ),
                                 QString( "%1 mm" ).arg( getMlvFocalLength( m_pMlvObject ) ),
                                 QString( "1/%1 s,  %2 deg,  %3 µs" ).arg( (uint16_t)(shutterSpeed + 0.5f) ).arg( (uint16_t)(shutterAngle + 0.5f) ).arg( getMlvShutter( m_pMlvObject ) ),
                                 QString( "ƒ/%1" ).arg( getMlvAperture( m_pMlvObject ) / 100.0, 0, 'f', 1 ),
                                 isoInfo,
                                 dualIso,
                                 QString( "%1 bits,  %2" ).arg( getLosslessBpp( m_pMlvObject ) ).arg( getMlvCompression( m_pMlvObject ) ),
                                 QString( "%1-%2-%3 / %4:%5:%6" )
                                           .arg( getMlvTmYear(m_pMlvObject) )
                                           .arg( getMlvTmMonth(m_pMlvObject), 2, 10, QChar('0') )
                                           .arg( getMlvTmDay(m_pMlvObject), 2, 10, QChar('0') )
                                           .arg( getMlvTmHour(m_pMlvObject), 2, 10, QChar('0') )
                                           .arg( getMlvTmMin(m_pMlvObject), 2, 10, QChar('0') )
                                           .arg( getMlvTmSec(m_pMlvObject), 2, 10, QChar('0') ),
                                 audioText );

    //Set Clip Info to Dialog
    m_pInfoDialog->ui->tableWidget->item( 0, 1 )->setText( ACTIVE_CLIP->getElement( 2 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 1, 1 )->setText( ACTIVE_CLIP->getElement( 3 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 2, 1 )->setText( ACTIVE_CLIP->getElement( 4 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 3, 1 )->setText( ACTIVE_CLIP->getElement( 5 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 4, 1 )->setText( ACTIVE_CLIP->getElement( 6 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 5, 1 )->setText( ACTIVE_CLIP->getElement( 7 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 6, 1 )->setText( ACTIVE_CLIP->getElement( 8 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 7, 1 )->setText( ACTIVE_CLIP->getElement( 9 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 8, 1 )->setText( ACTIVE_CLIP->getElement( 10 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 9, 1 )->setText( dualIsoInfo );
    m_pInfoDialog->ui->tableWidget->item( 10, 1 )->setText( ACTIVE_CLIP->getElement( 13 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 11, 1 )->setText( QString( "%1 black,  %2 white" ).arg( getMlvOriginalBlackLevel( m_pMlvObject ) ).arg( getMlvOriginalWhiteLevel( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 12, 1 )->setText( ACTIVE_CLIP->getElement( 14 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 13, 1 )->setText( ACTIVE_CLIP->getElement( 15 ).toString() );
    m_pInfoDialog->ui->tableWidget->item( 14, 1 )->setText( ACTIVE_CLIP->getElement( 16 ).toString() + ",  " + ACTIVE_CLIP->getElement( 17 ).toString() );

    resultingResolution();

    //Adapt slider to clip and move to position 0
    ui->horizontalSliderPosition->setValue( 0 );
    ui->horizontalSliderPosition->setMaximum( getMlvFrames( m_pMlvObject ) - 1 );

    //Restart timer
    m_timerId = startTimer( (int)( 1000.0 / getFramerate() ) );

    //Set selected debayer type
    if( ui->actionAlwaysUseAMaZE->isChecked() )
    {
        setMlvAlwaysUseAmaze( m_pMlvObject );
    }
    else if( ui->actionUseNoneDebayer->isChecked() )
    {
        setMlvUseNoneDebayer( m_pMlvObject );
    }
    else if( ui->actionUseSimpleDebayer->isChecked() )
    {
        setMlvUseSimpleDebayer( m_pMlvObject );
    }
    else if( ui->actionUseLmmseDebayer->isChecked() )
    {
        setMlvUseLmmseDebayer( m_pMlvObject );
    }
    else if( ui->actionUseIgvDebayer->isChecked() )
    {
        setMlvUseIgvDebayer( m_pMlvObject );
    }
    else if( ui->actionUseRcdDebayer->isChecked() )
    {
        setMlvUseRcdDebayer( m_pMlvObject );
    }
    else if( ui->actionUseDcbDebayer->isChecked() )
    {
        setMlvUseDcbDebayer( m_pMlvObject );
    }
    else
    {
        setMlvDontAlwaysUseAmaze( m_pMlvObject );
    }

    //Init audio playback engine
    m_pAudioPlayback->initAudioEngine( m_pMlvObject );

    m_fileLoaded = true;

    //Audio Track
    paintAudioTrack();

    //Frame label
    drawFrameNumberLabel();

    //enable drawing
    m_dontDraw = false;

    //Enable export now
    ui->actionExport->setEnabled( true );
    ui->actionExportCurrentFrame->setEnabled( true );

    //If clip loaded, import receipt is enabled
    ui->actionImportReceipt->setEnabled( true );
    ui->actionExportReceipt->setEnabled( true );
    //If clip loaded, enable session save
    ui->actionSaveSession->setEnabled( true );
    ui->actionSaveAsSession->setEnabled( true );
    ui->actionSaveSessionMetadata->setEnabled( true );
    //Enable select all clips action
    ui->actionSelectAllClips->setEnabled( true );

    //Setup Gradient
    ui->spinBoxGradientX->setMaximum( getMlvWidth( m_pMlvObject ) + 1000 );
    ui->spinBoxGradientY->setMaximum( getMlvHeight( m_pMlvObject ) + 1000 );
    ui->checkBoxGradientEnable->setEnabled( true );
    ui->toolButtonGradientPaint->setEnabled( true );

    //Cut In & Out
    initCutInOut( getMlvFrames( m_pMlvObject ) );

    //Raw black & white level
    initRawBlackAndWhite();

    //Set raw black level auto correct button
    ui->toolButtonRawBlackAutoCorrect->setEnabled( isRawBlackLevelWrong() );

    //Give curves GUI a link to processing object
    ui->labelCurves->setProcessingObject( m_pProcessingObject );
    ui->labelHueVsHue->setProcessingObject( m_pProcessingObject );
    ui->labelHueVsSat->setProcessingObject( m_pProcessingObject );
    ui->labelHueVsLuma->setProcessingObject( m_pProcessingObject );
    ui->labelLumaVsSat->setProcessingObject( m_pProcessingObject );

    //Prepare crosses for bad pixel map
    BadPixelFileHandler::crossesPrepareAll( m_pMlvObject, &m_pBadPixelCrosses, m_pScene );

    m_frameChanged = true;

    return MLV_ERR_NONE;
}

//returns true if file is already in session
bool MainWindow::isFileInSession(QString fileName)
{
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        if( GET_CLIP(i)->getPath() == fileName )
        {
            return true;
        }
    }
    return false;
}

//Show the file in
int MainWindow::showFileInEditor(int row)
{
    if( SESSION_CLIP_COUNT <= 0 ) return 1;

    //Stop Playback
    ui->actionPlay->setChecked( false );
    //Save slider receipt
    if( !ACTIVE_RECEIPT->wasNeverLoaded() && !m_inClipDeleteProcess ) setReceipt( ACTIVE_RECEIPT );
    //Save new position in session
    int oldActive = SESSION_ACTIVE_CLIP_ROW;
    SET_ACTIVE_CLIP_IDX( row );
    //Open new MLV
    if( openMlv( GET_CLIP( row )->getPath() ) )
    {
        //If one file is selected, reselect the last one, else do nothing (export)
        //And if there is another file we can switch to...
        if( selectedClipsList().size() <= 1
         && SESSION_CLIP_COUNT > 1)
        {
            m_pSelectionModel->setCurrentIndex( m_pProxyModel->mapFromSource( m_pModel->index( oldActive, 0, QModelIndex() ) ), QItemSelectionModel::ClearAndSelect );
            if( !GET_CLIP( oldActive )->getReceipt()->wasNeverLoaded() ) showFileInEditor( oldActive );
        }
        return 1;
    }
    //Now set it was loaded once
    GET_RECEIPT( row )->setLoaded();
    //Set sliders to receipt
    setSliders( GET_RECEIPT( row ), false );

    //Repaint the tables
    ui->listViewSession->reset();
    on_actionShowRedClips_toggled( ui->actionShowRedClips->isChecked() );
    on_actionShowYellowClips_toggled( ui->actionShowYellowClips->isChecked() );
    on_actionShowGreenClips_toggled( ui->actionShowGreenClips->isChecked() );
    on_actionShowUnmarkedClips_toggled( ui->actionShowUnmarkedClips->isChecked() );
    ui->tableViewSession->reset();
    m_pSelectionModel->setCurrentIndex( m_pProxyModel->mapFromSource( m_pModel->index( row, 0, QModelIndex() ) ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    //Focus Pixel Check
    focusPixelCheckAndInstallation();

    //Autoresize columns
    ui->tableViewSession->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );

    return 0;
}

//Handles preview pictures - make sure that right clip for row is loaded before!
void MainWindow::previewPicture( int row )
{
    //disable low level raw fixes for preview
    m_pMlvObject->llrawproc->fix_raw = 0;

    // Get proper image size
    int raw_w = m_pMlvObject->RAWI.xRes;
    int raw_h = m_pMlvObject->RAWI.yRes;
    int downscaled_factor = 1;

    if (raw_w > 2000 && raw_h > 1500) downscaled_factor = 9;
    else if (raw_w < 2000 && raw_h < 1500) downscaled_factor = 5;
    else downscaled_factor = 7;

    // For get_area_average_downscale_thumnail only: other factors for dualiso hiding the horizontal lines
    if (m_pMlvObject->llrawproc->dual_iso > 0)
    {
        if (downscaled_factor > 5) downscaled_factor = 8;
        else downscaled_factor = 4;
    }


    int width = raw_w / downscaled_factor;
    int height = raw_h / downscaled_factor;

    //Get frame from library, temp disable linear gradient and vignette, because not compatible with shrinked resolutions
    auto vstr = m_pMlvObject->processing->vignette_strength;
    auto gren = m_pMlvObject->processing->gradient_enable;
    m_pMlvObject->processing->vignette_strength = 0;
    m_pMlvObject->processing->gradient_enable = 0;
    //create_thumbnail( m_pMlvObject, m_pRawImage, downscaled_factor, width, height, QThread::idealThreadCount() );
    get_area_average_downscale_thumnail(m_pMlvObject, 0, downscaled_factor, QThread::idealThreadCount(), m_pRawImage);
    m_pMlvObject->processing->vignette_strength = vstr;
    m_pMlvObject->processing->gradient_enable = gren;

    QImage img( m_pRawImage,
                width,
                height,
                width * 3,
                QImage::Format_RGB888 );

    QPixmap pic = QPixmap::fromImage( img.scaled(
                        width * getHorizontalStretchFactor(true),
                        height * getVerticalStretchFactor(true),
                        Qt::IgnoreAspectRatio,
                        Qt::SmoothTransformation)
                    );

    pic.setDevicePixelRatio( devicePixelRatio() );
    m_pModel->setData( m_pModel->index( row, 0, QModelIndex() ), QIcon( pic ), Qt::DecorationRole );

    setPreviewMode();
}

//"one of the most important features": creating batch MAPP files
void MainWindow::on_actionCreateAllMappFilesNow_triggered()
{
    //Save current clip, to get back to this clip when ready
    int lastClip = SESSION_ACTIVE_CLIP_ROW;
    bool mapp = ui->actionCreateMappFiles->isChecked();
    ui->actionCreateMappFiles->setChecked( true );

    //Block GUI
    setEnabled( false );

    m_pStatusDialog->setEnabled( true );
    m_pStatusDialog->ui->label->setText( "Creating MAPP files..." );
    m_pStatusDialog->ui->labelEstimatedTime->setText( "" );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->ui->pushButtonAbort->setVisible( false );
    m_pStatusDialog->open();

    //Open all clips
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        qApp->processEvents();
        showFileInEditor( i );
        m_pStatusDialog->ui->progressBar->setValue( 100 * i / SESSION_CLIP_COUNT );
    }

    //Hide Status Dialog
    m_pStatusDialog->close();
    m_pStatusDialog->ui->pushButtonAbort->setVisible( true );

    //Go back to where we started
    showFileInEditor( lastClip );
    ui->actionCreateMappFiles->setChecked( mapp );

    //Unblock GUI
    setEnabled( true );
}

//Open a window which uses raw2mlv binary
void MainWindow::on_actionTranscodeAndImport_triggered()
{
    TranscodeDialog *pTranscode = new TranscodeDialog( this );
    pTranscode->exec();
    QStringList list = pTranscode->importList();
    openMlvSet( list );
    delete pTranscode;
}
