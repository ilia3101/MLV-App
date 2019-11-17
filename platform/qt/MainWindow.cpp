/*!
 * \file MainWindow.cpp
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
#include <QDesktopWidget>
#include <QScrollBar>
#include <QScreen>
#include <QMimeData>
#include <QDir>
#include <QSpacerItem>
#include <QDate>
#include <unistd.h>
#include <math.h>

#ifdef Q_OS_MACX
#include "AvfLibWrapper.h"
#include "MainWindow.h"
#endif

#include "SystemMemory.h"
#include "ExportSettingsDialog.h"
#include "EditSliderValueDialog.h"
#include "DarkStyle.h"
#include "DarkStyleModern.h"
#include "Updater/updaterUI/cupdaterdialog.h"
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

#define APPNAME "MLV App"
#define VERSION "1.9"
#define GITVERSION "QTv1.9"

#define FACTOR_DS       22.5
#define FACTOR_LS       11.2
#define FACTOR_LIGHTEN  0.6

//Constructor
MainWindow::MainWindow(int &argc, char **argv, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    //Change working directory for C part
    chdir( QCoreApplication::applicationDirPath().toLatin1().data() );

    ui->setupUi(this);
    setAcceptDrops(true);
    qApp->installEventFilter( this );

    //Set bools for draw rules
    m_dontDraw = true;
    m_frameStillDrawing = false;
    m_frameChanged = false;
    m_fileLoaded = false;
    m_fpsOverride = false;
    m_inOpeningProcess = false;
    m_setSliders = false;
    m_zoomTo100Center = false;
    m_zoomModeChanged = false;
    m_tryToSyncAudio = false;
    m_playbackStopped = false;

#ifdef STDOUT_SILENT
    //QtNetwork: shut up please!
    QLoggingCategory::setFilterRules(QStringLiteral("qt.network.ssl=false"));
#endif

    //Set Render Thread
    m_pRenderThread = new RenderFrameThread();
    m_pRenderThread->start();
    connect( m_pRenderThread, SIGNAL(frameReady()), this, SLOT(drawFrameReady()) );
    while( !m_pRenderThread->isRunning() ) {}

    //Init scripting engine
    m_pScripting = new Scripting( this );
    m_pScripting->scanScripts();

    //Init the GUI
    initGui();

    //Init the lib
    initLib();

    //Setup Toning (has to be done after initLib())
    on_horizontalSliderTone_valueChanged( 0 );

    //Setup AudioPlayback
    m_pAudioPlayback = new AudioPlayback( this );

    //Set timers
    m_timerId = startTimer( 40 ); //25fps initially only, is set after import
    m_timerCacheId = startTimer( 1000 ); //1fps

    //Connect Export Handler
    connect( this, SIGNAL(exportReady()), this, SLOT(exportHandler()) );

    //"Open with" for Windows or scripts
    if( argc > 1 )
    {
        QString fileName = QString( "%1" ).arg( argv[1] );

        //Exit if not an MLV file or aborted
        if( QFile(fileName).exists() && fileName.endsWith( ".mlv", Qt::CaseInsensitive ) )
        {
            importNewMlv( fileName );
            //Show last imported file
            if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            m_inOpeningProcess = true;
            openSession( fileName );
            //Show last imported file
            if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );
            m_inOpeningProcess = false;
            m_sessionFileName = fileName;
            selectDebayerAlgorithm();
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".command", Qt::CaseInsensitive ) )
        {
            if( m_pScripting->installScript( fileName ) )
                QMessageBox::information( this, APPNAME, tr( "Installation of script %1 successful." ).arg( QFileInfo( fileName ).fileName() ) );
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".fpm", Qt::CaseInsensitive ) )
        {
            if( FpmInstaller::installFpm( fileName ) )
                QMessageBox::information( this, APPNAME, tr( "Installation of focus pixel map %1 successful." ).arg( QFileInfo( fileName ).fileName() ) );
        }
    }

    //Update check, if autocheck enabled, once a day
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    QString date = set.value( "lastUpdateCheck", QString( "" ) ).toString();
    if( ui->actionAutoCheckForUpdates->isChecked() && date != QDate::currentDate().toString() )
    {
        m_pUpdateCheck = new CUpdater( this, QString( "https://github.com/ilia3101/MLV-App" ), GITVERSION );
        connect( m_pUpdateCheck, SIGNAL(updateAvailable(bool)), this, SLOT(updateCheckResponse(bool)) );
        m_pUpdateCheck->checkForUpdates();
    }
}

//Destructor
MainWindow::~MainWindow()
{
    killTimer( m_timerId );
    killTimer( m_timerCacheId );

    //End Render Thread
    m_frameStillDrawing = false;
    disconnect( m_pRenderThread, SIGNAL(frameReady()), this, SLOT(drawFrameReady()) );
    m_pRenderThread->stop();
    while( !m_pRenderThread->isFinished() ) {}
    delete m_pRenderThread;

    //Save settings
    writeSettings();
    delete m_pScripting;
    delete m_pReceiptClipboard;
    delete m_pCopyMask;
    delete m_pAudioPlayback;
    delete m_pAudioWave;
    delete m_pGradientElement;
    delete m_pStatusDialog;
    delete m_pInfoDialog;
    delete ui;
}

//Called from timer and frame ready: initiate drawing next frame
void MainWindow::timerFrameEvent( void )
{
    static QTime lastTime;              //Last Time a picture was rendered
    static int timeDiff = 0;            //TimeDiff between 2 rendered frames in Playback

    if( m_frameStillDrawing )
    {
        //On setup slider priority
        if( !ui->actionPlay->isChecked() )
        {
            return;
        }
        //else fast playback priority -> frame n+1 will be calculated as soon as frame n is ready
        connect( this, SIGNAL(frameReady()), this, SLOT(timerFrameEvent()) );
        return;
    }
    else
    {
        //disconnect the link from above again
        disconnect( this, SIGNAL(frameReady()), this, SLOT(timerFrameEvent()) );
    }
    if( !m_exportQueue.empty() ) return;

    //Time measurement
    QTime nowTime = QTime::currentTime();
    timeDiff = lastTime.msecsTo( nowTime );

    //Playback
    playbackHandling( timeDiff );

    //Give free one core for responsive GUI
    if( m_frameChanged )
    {
        m_countTimeDown = 3; //3 secs
        int cores = QThread::idealThreadCount();
        if( cores > 1 ) cores -= 1; // -1 for the processing
        setMlvCpuCores( m_pMlvObject, cores );
    }

    //Trigger Drawing
    if( m_frameChanged && !m_dontDraw && !m_inOpeningProcess )
    {
        m_frameChanged = false; //first do this, if there are changes between rendering
        drawFrame();
        //Allow interaction while playback
        //qApp->processEvents();

        //fps measurement
        if( timeDiff != 0 ) m_pFpsStatus->setText( tr( "Playback: %1 fps" ).arg( (int)( 1000 / lastTime.msecsTo( nowTime ) ) ) );
        lastTime = nowTime;

        //When playback is off, the timeDiff is set to 0 for DropFrameMode
        if( !ui->actionPlay->isChecked() ) timeDiff = 1000 / getFramerate();
    }
    else
    {
        m_pFpsStatus->setText( tr( "Playback: 0 fps" ) );
        lastTime = QTime::currentTime(); //do that for calculation of timeDiff for DropFrameMode;

    }
}

//Timer
void MainWindow::timerEvent(QTimerEvent *t)
{
    //Main timer
    if( t->timerId() == m_timerId )
    {
        timerFrameEvent();
        return;
    }
    //1sec Timer
    else if( t->timerId() == m_timerCacheId )
    {
        //Caching Status Label
        if( m_fileLoaded && isMlvObjectCaching( m_pMlvObject ) > 0 )
        {
            m_pCachingStatus->setText( tr( "Caching: active" ) );
        }
        else
        {
            m_pCachingStatus->setText( tr( "Caching: idle" ) );
        }

        if( m_fileLoaded )
        {
            //get all cores again
            if( m_countTimeDown == 0 ) setMlvCpuCores( m_pMlvObject, QThread::idealThreadCount() );
            if( m_countTimeDown >= 0 ) m_countTimeDown--;
        }
    }
}

//Window resized -> scale picture
void MainWindow::resizeEvent(QResizeEvent *event)
{
    //If opening files just quit here
    if( m_inOpeningProcess )
    {
        event->accept();
        return;
    }

    //Stop playback if active
    ui->actionPlay->setChecked( false );
    m_pAudioPlayback->stop(); //Stop audio explicitely

    if( m_fileLoaded )
    {
        drawFrame();
        if( ui->checkBoxGradientEnable->isChecked() && ui->groupBoxLinearGradient->isChecked() )
        {
            while( m_frameStillDrawing ) qApp->processEvents();
            m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                                       m_pScene->height(),
                                                       getMlvWidth( m_pMlvObject ),
                                                       getMlvHeight( m_pMlvObject ) );
        }
    }
    event->accept();
}

// Intercept FileOpen events
bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen)
    {
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
        //Exit if not an MLV file or aborted
        QString fileName = openEvent->file();
        if( QFile(fileName).exists() && fileName.endsWith( ".mlv", Qt::CaseInsensitive ) )
        {
            importNewMlv( fileName );
            //Show last imported file
            if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );
            //Caching is in which state? Set it!
            if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            m_inOpeningProcess = true;
            openSession( fileName );
            //Show last imported file
            if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );
            //Caching is in which state? Set it!
            if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
            m_sessionFileName = fileName;
            m_inOpeningProcess = false;
            selectDebayerAlgorithm();
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".command", Qt::CaseInsensitive ) )
        {
            if( m_pScripting->installScript( fileName ) )
                QMessageBox::information( this, APPNAME, tr( "Installation of script %1 successful." ).arg( QFileInfo( fileName ).fileName() ) );
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".fpm", Qt::CaseInsensitive ) )
        {
            if( FpmInstaller::installFpm( fileName ) )
                QMessageBox::information( this, APPNAME, tr( "Installation of focus pixel map %1 successful." ).arg( QFileInfo( fileName ).fileName() ) );
        }
        else return false;
    }
    return QMainWindow::event(event);
}

//The dragEnterEvent() function is typically used to inform Qt about the types of data that the widget accepts
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

//The dropEvent() is used to unpack dropped data and handle it in way that is suitable for your application.
void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList list;
    if( event->mimeData()->urls().count() > 0 )
    {
        if( event->mimeData()->urls().at(0).path().endsWith( ".MLV", Qt::CaseInsensitive )
         || event->mimeData()->urls().at(0).path().endsWith( ".FPM", Qt::CaseInsensitive )
         || event->mimeData()->urls().at(0).path().endsWith( ".COMMAND", Qt::CaseInsensitive ) )
        {
            for( int i = 0; i < event->mimeData()->urls().count(); i++ )
            {
                list.append( event->mimeData()->urls().at(i).path() );
            }
            openMlvSet( list );
        }
        else if( event->mimeData()->urls().at(0).path().endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            m_inOpeningProcess = true;
            openSession( event->mimeData()->urls().at(0).path() );
            //Show last imported file
            if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );
            m_inOpeningProcess = false;
            m_sessionFileName = event->mimeData()->urls().at(0).path();
            selectDebayerAlgorithm();
        }
    }
    event->acceptProposedAction();
}

//Open a couple of MLVs
void MainWindow::openMlvSet( QStringList list )
{
    m_inOpeningProcess = true;
    for( int i = 0; i < list.count(); i++ )
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
                for( int i = 0; i < list.count(); i++ ) files.append( QString( "\r\n%1" ).arg( QFileInfo( list.at(i) ).fileName() ) );
                QMessageBox::information( this, APPNAME, tr( "Installation of focus pixel map(s) %1\r\nsuccessful." ).arg( files ) );
            }
            m_inOpeningProcess = false;
            return;
        }

        if( i == 0 && QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            openSession( fileName );
        }
        else
        {
            //Exit if not an MLV file or aborted
            if( fileName == QString( "" ) || !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) continue;
            importNewMlv( fileName );
        }
    }

    if( m_pSessionReceipts.count() )
    {
        //Show last imported file
        showFileInEditor( m_pSessionReceipts.count() - 1 );
    }

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//App shall close -> hammer method, we shot on the main class... for making the app close and killing everything in background
void MainWindow::closeEvent(QCloseEvent *event)
{
    ui->actionPlay->setChecked( false );
    on_actionPlay_triggered( false );

    //If user wants to be asked
    if( ui->actionAskForSavingOnQuit->isChecked() && ui->listWidgetSession->count() != 0 )
    {
        //Ask before quit
        int ret = QMessageBox::warning( this, APPNAME, tr( "Do you really like to quit MLVApp? Do you like to save the session?" ),
                                        tr( "Cancel" ), tr( "Quit without saving" ), tr( "Save and quit" ) );
        //Aborted
        if( ret == QMessageBox::Escape || ret == 0 )
        {
            event->ignore();
            return;
        }
        //Save and quit
        else if( ret == 2 )
        {
            on_actionSaveSession_triggered();
            //Saving was aborted -> abort quit
            if( m_sessionFileName.count() == 0 )
            {
                event->ignore();
                return;
            }
        }
    }

    qApp->quit();
    event->accept();
}

//Disable WBPicker if picture is left and if mouse is clicked somewhere else
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED( watched );
    if( event->type() == QEvent::MouseMove )
    {
        static bool graphicsViewReached = false;
        if( ui->graphicsView->underMouse() ) graphicsViewReached = true;
        if( !ui->graphicsView->underMouse() && graphicsViewReached )
        {
            graphicsViewReached = false;
            ui->toolButtonWb->setChecked( false );
            ui->actionWhiteBalancePicker->setChecked( false );
        }
    }
    else if( event->type() == QEvent::MouseButtonPress )
    {
        if( !ui->graphicsView->underMouse()
         && !ui->toolButtonWb->underMouse()
         && ui->actionWhiteBalancePicker->isChecked() )
        {
            ui->toolButtonWb->setChecked( false );
            ui->actionWhiteBalancePicker->setChecked( false );
        }

    }
    else if( event->type() == QEvent::Resize
          && ( watched == ui->dockWidgetEdit || watched == ui->dockWidgetSession ) )
    {
        /*QResizeEvent *resizeEvent = static_cast<QResizeEvent*>(event);
        qDebug("Dock Resized (New Size) - Width: %d Height: %d",
               resizeEvent->size().width(),
               resizeEvent->size().height());*/
        //setPreviewMode();
        m_frameChanged = true;
    }
    return QWidget::eventFilter(watched, event);
}

//Draw a raw picture to the gui -> start render thread
void MainWindow::drawFrame( void )
{
    m_frameStillDrawing = true;

    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //Get frame from library
    if( ui->actionPlay->isChecked() && ui->actionDropFrameMode->isChecked() )
    {
        //If we are in playback, dropmode, we calculated the exact frame to sync the timeline
        m_pRenderThread->renderFrame( m_newPosDropMode );

        //Draw TimeCode
        if( !m_tcModeDuration )
        {
            QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( m_newPosDropMode, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                              30 * devicePixelRatio(),
                                                                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );

            pic.setDevicePixelRatio( devicePixelRatio() );
            m_pTcLabel->setPixmap( pic );
        }
    }
    else
    {
        //Else we render the frame which is selected by the slider
        m_pRenderThread->renderFrame( ui->horizontalSliderPosition->value() );

        //Draw TimeCode
        if( !m_tcModeDuration )
        {
            QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->horizontalSliderPosition->value(), getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                              30 * devicePixelRatio(),
                                                                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
            pic.setDevicePixelRatio( devicePixelRatio() );
            m_pTcLabel->setPixmap( pic );
        }
    }
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
        if( !openMlvForPreview( fileName ) )
        {
            //Save last file name
            m_lastMlvOpenFileName = fileName;

            on_actionResetReceipt_triggered();

            //Set to "please load when info is there"
            m_pSessionReceipts.last()->setFocusPixels( -1 );
            m_pSessionReceipts.last()->setStretchFactorY( -1 );

            previewPicture( ui->listWidgetSession->count() - 1 );
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
#ifdef Q_OS_UNIX
    mlvObject_t * new_MlvObject = initMlvObjectWithClip( fileName.toUtf8().data(), MLV_OPEN_PREVIEW, &mlvErr, mlvErrMsg );
#else
    mlvObject_t * new_MlvObject = initMlvObjectWithClip( fileName.toLatin1().data(), MLV_OPEN_PREVIEW, &mlvErr, mlvErrMsg );
#endif
    if( mlvErr )
    {
        QMessageBox::critical( this, tr( "MLV Error" ), tr( "%1" ).arg( mlvErrMsg ), tr("Cancel") );
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
                                                    tr("Magic Lantern Video (*.mlv *.MLV)") );

    if( files.empty() ) return;

    m_inOpeningProcess = true;

    for( int i = 0; i < files.count(); i++ )
    {
        QString fileName = files.at(i);

        //Exit if not an MLV file or aborted
        if( fileName == QString( "" ) || !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) continue;

        importNewMlv( fileName );
    }

    //Show last imported file
    if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );

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

    for( int i = 0; i < files.count(); i++ )
    {
        QString fileName = files.at(i);

        //Exit if not an MLV file or aborted
        if( fileName == QString( "" ) || !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) continue;

        importNewMlv( fileName );
    }

    //Show last imported file
    if( m_pSessionReceipts.count() ) showFileInEditor( m_pSessionReceipts.count() - 1 );

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Open an assistant, which helps selection clips in session in dependency to clips which were used in FCPXML project
void MainWindow::on_actionFcpxmlSelectionAssistant_triggered()
{
    FcpxmlSelectDialog *sd = new FcpxmlSelectDialog( this, ui->listWidgetSession );
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
#ifdef Q_OS_UNIX
    mlvObject_t * new_MlvObject = initMlvObjectWithClip( fileName.toUtf8().data(), mlvOpenMode, &mlvErr, mlvErrMsg );
#else
    mlvObject_t * new_MlvObject = initMlvObjectWithClip( fileName.toLatin1().data(), mlvOpenMode, &mlvErr, mlvErrMsg );
#endif
    if( mlvErr )
    {
        QMessageBox::critical( this, tr( "MLV Error" ), tr( "%1" ).arg( mlvErrMsg ), tr("Cancel") );
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
    int isoVal = (int)getMlvIso( m_pMlvObject );
    int disoVal = (int)getMlv2ndIso( m_pMlvObject );
    QString isoInfo = QString( "%1" ).arg( isoVal );
    if( llrpGetDualIsoValidity( m_pMlvObject ) == DISO_VALID )
    {
        //disoVal choises for first two checks: "-6 EV", "-5 EV", "-4 EV", "-3 EV", "-2 EV", "-1 EV", "+1 EV", "+2 EV", "+3 EV", "+4 EV", "+5 EV", "+6 EV", "100", "200", "400", "800", "1600", "3200", "6400", "12800", "25600"
        if(disoVal < 0) //Menu index mode relative ISO
        {
            uint32_t recoveryIsoVal = isoVal * pow (2, disoVal);
            if(recoveryIsoVal < 100) recoveryIsoVal = 100;
            isoInfo = QString( "%1/%2, Dual Iso" ).arg( getMlvIso( m_pMlvObject ) ).arg( recoveryIsoVal );
        }
        else if( disoVal && (disoVal < 100) ) //Menu index mode absolute ISO
        {
            uint32_t recoveryIsoVal = 100 * pow (2, disoVal);
            isoInfo = QString( "%1/%2, Dual Iso" ).arg( getMlvIso( m_pMlvObject ) ).arg( recoveryIsoVal );
        }
        else if( (disoVal >= 100) && (disoVal <= 25600) ) //Valid iso mode
        {
            isoInfo = QString( "%1/%2, Dual Iso" ).arg( getMlvIso( m_pMlvObject ) ).arg( disoVal );
        }
    }

    //Set Clip Info to Dialog
    m_pInfoDialog->ui->tableWidget->item( 0, 1 )->setText( QString( "%1" ).arg( (char*)getMlvCamera( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 1, 1 )->setText( QString( "%1" ).arg( (char*)getMlvLens( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 2, 1 )->setText( QString( "%1 x %2 pixels" ).arg( (int)getMlvWidth( m_pMlvObject ) ).arg( (int)getMlvHeight( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 3, 1 )->setText( QString( "%1" ).arg( m_pTimeCodeImage->getTimeCodeFromFps( (int)getMlvFrames( m_pMlvObject ), getMlvFramerate( m_pMlvObject ) ) ) );
    m_pInfoDialog->ui->tableWidget->item( 4, 1 )->setText( QString( "%1" ).arg( (int)getMlvFrames( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 5, 1 )->setText( QString( "%1 fps" ).arg( getMlvFramerate( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 6, 1 )->setText( QString( "%1 mm" ).arg( getMlvFocalLength( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 7, 1 )->setText( QString( "1/%1 s,  %2 deg,  %3 µs" ).arg( (uint16_t)(shutterSpeed + 0.5f) ).arg( (uint16_t)(shutterAngle + 0.5f) ).arg( getMlvShutter( m_pMlvObject )) );
    m_pInfoDialog->ui->tableWidget->item( 8, 1 )->setText( QString( "ƒ/%1" ).arg( getMlvAperture( m_pMlvObject ) / 100.0, 0, 'f', 1 ) );
    m_pInfoDialog->ui->tableWidget->item( 9, 1 )->setText( isoInfo );
    m_pInfoDialog->ui->tableWidget->item( 10, 1 )->setText( QString( "%1 bits,  %2" ).arg( getLosslessBpp( m_pMlvObject ) ).arg( getMlvCompression( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 11, 1 )->setText( QString( "%1 black,  %2 white" ).arg( getMlvOriginalBlackLevel( m_pMlvObject ) ).arg( getMlvOriginalWhiteLevel( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 12, 1 )->setText( QString( "%1-%2-%3 / %4:%5:%6" )
                                                            .arg( getMlvTmYear(m_pMlvObject) )
                                                            .arg( getMlvTmMonth(m_pMlvObject), 2, 10, QChar('0') )
                                                            .arg( getMlvTmDay(m_pMlvObject), 2, 10, QChar('0') )
                                                            .arg( getMlvTmHour(m_pMlvObject), 2, 10, QChar('0') )
                                                            .arg( getMlvTmMin(m_pMlvObject), 2, 10, QChar('0') )
                                                            .arg( getMlvTmSec(m_pMlvObject), 2, 10, QChar('0') ) );
    resultingResolution();

    if( doesMlvHaveAudio( m_pMlvObject ) )
    {
        m_pInfoDialog->ui->tableWidget->item( 13, 1 )->setText( QString( "%1 channel(s),  %2 kHz" )
                                                                .arg( getMlvAudioChannels( m_pMlvObject ) )
                                                                .arg( getMlvSampleRate( m_pMlvObject ) ) );
    }
    else
    {
        m_pInfoDialog->ui->tableWidget->item( 13, 1 )->setText( QString( "-" ) );
    }

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
    //If clip loaded, enable session save
    ui->actionSaveSession->setEnabled( true );
    ui->actionSaveAsSession->setEnabled( true );
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

    m_frameChanged = true;

    return MLV_ERR_NONE;
}

//Handles the playback and must be triggered from timer
void MainWindow::playbackHandling(int timeDiff)
{
    if( ui->actionPlay->isChecked() )
    {
        //when on last frame
        if( ui->horizontalSliderPosition->value() >= ui->spinBoxCutOut->value() - 1 )
        {
            if( ui->actionLoop->isChecked() )
            {
                //Loop, goto cut in
                ui->horizontalSliderPosition->setValue( ui->spinBoxCutIn->value() - 1 );
                if( ui->actionAudioOutput->isChecked() )m_newPosDropMode = ui->spinBoxCutIn->value() - 1;

                //Sync audio
                if( ui->actionAudioOutput->isChecked()
                 && ui->actionDropFrameMode->isChecked() )
                {
                    m_tryToSyncAudio = true;
                }
            }
            else
            {
                //Stop on last frame of clip
                ui->actionPlay->setChecked( false );
                m_pAudioPlayback->stop(); //Stop audio immediately, that is faster on Linux
            }
        }
        else
        {
            //Normal mode: next frame
            if( !ui->actionDropFrameMode->isChecked() )
            {
                ui->horizontalSliderPosition->setValue( ui->horizontalSliderPosition->value() + 1 );
                m_newPosDropMode = ui->horizontalSliderPosition->value(); //track it also, for mode changing
            }
            //Drop Frame Mode: calc picture for actual time
            else
            {
                //This is the exact frame we need on the time line NOW!
                m_newPosDropMode += (getFramerate() * (double)timeDiff / 1000.0);
                //Loop!
                if( ui->actionLoop->isChecked() && ( m_newPosDropMode >= ui->spinBoxCutOut->value() - 1 ) )
                {
                    m_newPosDropMode -= (ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value());
                    //Sync audio
                    if( ui->actionAudioOutput->isChecked() )
                    {
                        m_tryToSyncAudio = true;
                    }
                }
                //Limit to last frame if not in loop
                else if( m_newPosDropMode >= ui->spinBoxCutOut->value() - 1 )
                {
                    // -1 because 0 <= frame < ui->spinBoxCutOut->value()
                    m_newPosDropMode = ui->spinBoxCutOut->value() - 1;
                }
                //Because we need it NOW, block slider signals and draw after this function in this timerEvent
                ui->horizontalSliderPosition->blockSignals( true );
                ui->horizontalSliderPosition->setValue( m_newPosDropMode );
                ui->horizontalSliderPosition->blockSignals( false );
                m_frameChanged = true;
            }
        }
    }
    else
    {
        m_newPosDropMode = ui->horizontalSliderPosition->value(); //track it also when playback is off
    }
}

//Initialize the GUI
void MainWindow::initGui( void )
{
    //We dont want a context menu which could disable the menu bar
    setContextMenuPolicy(Qt::NoContextMenu);

    //Darktheme menu
    m_darkFrameGroup = new QActionGroup( this );
    m_darkFrameGroup->setExclusive( true );
    m_darkFrameGroup->addAction( ui->actionDarkThemeStandard );
    m_darkFrameGroup->addAction( ui->actionDarkThemeModern );
    ui->actionDarkThemeStandard->setChecked( true );

    //Preview debayer as group
    m_previewDebayerGroup = new QActionGroup( this );
    m_previewDebayerGroup->setExclusive( true );
    m_previewDebayerGroup->addAction( ui->actionUseNoneDebayer );
    m_previewDebayerGroup->addAction( ui->actionUseSimpleDebayer );
    m_previewDebayerGroup->addAction( ui->actionUseBilinear );
    m_previewDebayerGroup->addAction( ui->actionUseLmmseDebayer );
    m_previewDebayerGroup->addAction( ui->actionUseIgvDebayer );
    m_previewDebayerGroup->addAction( ui->actionUseAhdDebayer );
    m_previewDebayerGroup->addAction( ui->actionAlwaysUseAMaZE );
    m_previewDebayerGroup->addAction( ui->actionCaching );
    m_previewDebayerGroup->addAction( ui->actionDontSwitchDebayerForPlayback );
    ui->actionUseBilinear->setChecked( true );
    ui->actionCaching->setVisible( false );

    //Scope menu as group
    m_scopeGroup = new QActionGroup( this );
    m_scopeGroup->setExclusive( true );
    m_scopeGroup->addAction( ui->actionShowVectorScope );
    m_scopeGroup->addAction( ui->actionShowWaveFormMonitor );
    m_scopeGroup->addAction( ui->actionShowHistogram );
    m_scopeGroup->addAction( ui->actionShowParade );

    //Session List options as group
    m_sessionListGroup = new QActionGroup( this );
    m_sessionListGroup->setExclusive( true );
    m_sessionListGroup->addAction( ui->actionPreviewDisabled );
    m_sessionListGroup->addAction( ui->actionPreviewList );
    m_sessionListGroup->addAction( ui->actionPreviewPicture );
    m_sessionListGroup->addAction( ui->actionPreviewPictureBottom );

    //Playback element as group
    m_playbackElementGroup = new QActionGroup( this );
    m_playbackElementGroup->setExclusive( true );
    m_playbackElementGroup->addAction( ui->actionTimecodePositionMiddle );
    m_playbackElementGroup->addAction( ui->actionTimecodePositionRight );

#ifdef Q_OS_LINUX
    //if not doing this, some elements are covered by the scrollbar on Linux only
    ui->dockWidgetEdit->setMinimumWidth( 240 );
    ui->dockWidgetContents->setMinimumWidth( 240 );
#endif

    //Dock area behavior
    setCorner( Qt::TopLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::BottomLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );

    //Init the Dialogs
    m_pInfoDialog = new InfoDialog( this );
    m_pStatusDialog = new StatusDialog( this );
    m_pCopyMask = new ReceiptCopyMaskDialog( this );
    ui->actionShowHistogram->setChecked( true );

    //Export abort connection
    connect( m_pStatusDialog, SIGNAL(abortPressed()), this, SLOT(exportAbort()) );

    //AudioTrackWave
    m_pAudioWave = new AudioWave();
    QPixmap pic = QPixmap::fromImage( m_pAudioWave->getMonoWave( NULL, 0, 100, devicePixelRatio() ) );
    pic.setDevicePixelRatio( devicePixelRatio() );
    ui->labelAudioTrack->setPixmap( pic );
    //Fullscreen does not work well, so disable
    ui->actionFullscreen->setVisible( false );
    //Disable caching by default to avoid crashes
    //ui->actionCaching->setVisible( false );
    //Hide deflicker target - no one knows what it does...
    ui->spinBoxDeflickerTarget->setVisible( false );
    ui->DeflickerTargetLabel->setVisible( false );
    ui->line_11->setVisible( false );
    //Disable unused (for now) actions
    ui->actionPasteReceipt->setEnabled( false );
    //Disable export until file opened!
    ui->actionExport->setEnabled( false );
    ui->actionExportCurrentFrame->setEnabled( false );
    //Set fit to screen as default zoom
    ui->actionZoomFit->setChecked( true );
    //If no clip loaded, import receipt is disabled
    ui->actionImportReceipt->setEnabled( false );
    //If no clip loaded, disable session save
    ui->actionSaveSession->setEnabled( false );
    ui->actionSaveAsSession->setEnabled( false );
    //Set tooltips
    ui->toolButtonCutIn->setToolTip( tr( "Set Cut In    %1" ).arg( ui->toolButtonCutIn->shortcut().toString() ) );
    ui->toolButtonCutOut->setToolTip( tr( "Set Cut Out    %1" ).arg( ui->toolButtonCutOut->shortcut().toString() ) );
    //Set disabled select all and delete clip
    ui->actionDeleteSelectedClips->setEnabled( false );
    ui->actionSelectAllClips->setEnabled( false );
    //disable lut as default
    ui->toolButtonLoadLut->setEnabled( false );
    ui->lineEditLutName->setEnabled( false );
    ui->label_LutStrengthText->setEnabled( false );
    ui->label_LutStrengthVal->setEnabled( false );
    ui->horizontalSliderLutStrength->setEnabled( false );
    //disable filter as default
    ui->comboBoxFilterName->setEnabled( false );
    ui->label_FilterStrengthVal->setEnabled( false );
    ui->label_FilterStrengthText->setEnabled( false );
    ui->horizontalSliderFilterStrength->setEnabled( false );

    //Hide DualIso Fullres Blending (only brings black frame if off)
    ui->DualISOFullresBlendingLabel->setVisible( false );
    ui->toolButtonDualIsoFullresBlending->setVisible( false );
    ui->toolButtonDualIsoFullresBlendingOff->setVisible( false );
    ui->toolButtonDualIsoFullresBlendingOn->setVisible( false );

    //Set up image in GUI
    QImage image(":/IMG/IMG/histogram.png");
    m_pGraphicsItem = new QGraphicsPixmapItem( QPixmap::fromImage(image) );
    m_pScene = new GraphicsPickerScene( this );
    m_pScene->addItem( m_pGraphicsItem );
    ui->graphicsView->setScene( m_pScene );
    ui->graphicsView->show();
    connect( ui->graphicsView, SIGNAL( customContextMenuRequested(QPoint) ), this, SLOT( pictureCustomContextMenuRequested(QPoint) ) );
    connect( m_pScene, SIGNAL( wbPicked(int,int) ), this, SLOT( whiteBalancePicked(int,int) ) );
    connect( m_pScene, SIGNAL( filesDropped(QStringList) ), this, SLOT( openMlvSet(QStringList) ) );

    //Prepare gradient elements
    QPolygon polygon;
    m_pGradientElement = new GradientElement( polygon );
    m_pScene->addItem( m_pGradientElement->gradientGraphicsElement() );
    connect( m_pScene, SIGNAL( gradientAnchor(int,int) ), this, SLOT( gradientAnchorPicked(int,int) ) );
    connect( m_pScene, SIGNAL( gradientFinalPos(int,int,bool) ), this, SLOT( gradientFinalPosPicked(int,int,bool) ) );
    connect( m_pGradientElement->gradientGraphicsElement(), SIGNAL( itemMoved(int,int) ), this, SLOT( gradientGraphicElementMoved(int,int) ) );
    connect( m_pGradientElement->gradientGraphicsElement(), SIGNAL( itemHovered(bool) ), this, SLOT( gradientGraphicElementHovered(bool) ) );
    //Disable Gradient while no file loaded
    ui->checkBoxGradientEnable->setChecked( false );
    ui->checkBoxGradientEnable->setEnabled( false );
    ui->toolButtonGradientPaint->setEnabled( false );

    //Layer for Bad Pixel visualization
    QImage image2(":/IMG/IMG/PixelSelector.png");
    m_pGraphicsBadPixelItem = new QGraphicsPixmapItem( QPixmap::fromImage( image2 ) );
    m_pGraphicsBadPixelItem->moveBy( 100, 100 );
    m_pScene->addItem( m_pGraphicsBadPixelItem );
    m_pGraphicsBadPixelItem->hide();

    //Cut In & Out
    initCutInOut( -1 );

    //Set up caching status label
    m_pCachingStatus = new QLabel( statusBar() );
    m_pCachingStatus->setMaximumWidth( 100 );
    m_pCachingStatus->setMinimumWidth( 100 );
    m_pCachingStatus->setText( tr( "Caching: idle" ) );
    //m_pCachingStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusBar()->addWidget( m_pCachingStatus );
    m_pCachingStatus->hide(); //delete this line, if caching is available again one day

    //Set up fps status label
    m_pFpsStatus = new QLabel( statusBar() );
    m_pFpsStatus->setMaximumWidth( 110 );
    m_pFpsStatus->setMinimumWidth( 110 );
    m_pFpsStatus->setText( tr( "Playback: 0 fps" ) );
    //m_pFpsStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusBar()->addWidget( m_pFpsStatus );

    //Set up frame number status label
    m_pFrameNumber = new QLabel( statusBar() );
    m_pFrameNumber->setMaximumWidth( 120 );
    m_pFrameNumber->setMinimumWidth( 120 );
    drawFrameNumberLabel();
    //m_pFpsStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusBar()->addWidget( m_pFrameNumber );

    //Set up chosen debayer status label
    m_pChosenDebayer = new QLabel( statusBar() );
    m_pChosenDebayer->setMaximumWidth( 120 );
    m_pChosenDebayer->setMinimumWidth( 120 );
    m_pChosenDebayer->setText( tr( "AMaZE" ) );
    m_pChosenDebayer->setToolTip( tr( "Current debayer algorithm." ) );
    statusBar()->addWidget( m_pChosenDebayer );

    //Recent sessions menu
    m_pRecentFilesMenu = new QRecentFilesMenu(tr("Recent Sessions"), ui->menuFile);

    //Read Settings
    readSettings();

    //Add recent sessions to filemenu
    ui->menuFile->insertMenu( ui->actionSaveSession, m_pRecentFilesMenu );
    connect( m_pRecentFilesMenu, SIGNAL(recentFileTriggered(const QString &)), this, SLOT(openRecentSession(const QString &)) );

    //Init clipboard
    m_pReceiptClipboard = new ReceiptSettings();

    //Init session settings
    m_pSessionReceipts.clear();

    //Reset session name
    m_sessionFileName.clear();

    //Init Export Queue
    m_exportQueue.clear();

    //TimeCode Label
    m_pTcLabel = new DoubleClickLabel( this );
    m_pTcLabel->setToolTip( tr( "Timecode/Duration(edited) h:m:s.frame - change by doubleclicking" ) );
    m_pTcLabel->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( m_pTcLabel, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(mpTcLabel_customContextMenuRequested(QPoint)) );
    connect( m_pTcLabel, SIGNAL(doubleClicked()), this, SLOT(tcLabelDoubleClicked()) );
    m_tcModeDuration = false;
    if( m_timeCodePosition == 1 )
    {
        //TC between buttons
        QWidget* spacer1 = new QWidget();
        spacer1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        spacer1->setMaximumWidth( 5 );
        ui->mainToolBar->insertWidget( ui->actionGoto_First_Frame, spacer1 );
        ui->mainToolBar->insertWidget( ui->actionGoto_First_Frame, m_pTcLabel );
        QWidget* spacer2 = new QWidget();
        spacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        spacer2->setMaximumWidth( 5 );
        ui->mainToolBar->insertWidget( ui->actionGoto_First_Frame, spacer2 );
        ui->actionTimecodePositionMiddle->setChecked( true );
    }
    else
    {
        //TC total right
        QWidget* spacer1 = new QWidget();
        spacer1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        ui->mainToolBar->insertWidget( ui->actionGoto_First_Frame, spacer1 );
        QWidget* spacer2 = new QWidget();
        spacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        spacer2->setMaximumWidth( 5 );
        ui->mainToolBar->addWidget( spacer2 );
        ui->mainToolBar->addWidget( m_pTcLabel );
        QWidget* spacer3 = new QWidget();
        spacer3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        spacer3->setMaximumWidth( 5 );
        ui->mainToolBar->addWidget( spacer3 );
        ui->actionTimecodePositionRight->setChecked( true );
    }

    m_pTimeCodeImage = new TimeCodeLabel();
    QPixmap picTc = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( 0, 25 ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
    picTc.setDevicePixelRatio( devicePixelRatio() );
    m_pTcLabel->setPixmap( picTc );

    //ColorWheels
    ui->labelColorWheelMaster->paintElement();
    ui->labelColorWheelShadows->paintElement();
    ui->labelColorWheelMidtones->paintElement();
    ui->labelColorWheelHighlights->paintElement();
    ui->groupBoxColorWheels->setVisible( false );

    //CurvesElement
    ui->labelCurves->setFrameChangedPointer( &m_frameChanged );
    ui->labelCurves->paintElement();

    //HueVsHue
    ui->labelHueVsHue->setFrameChangedPointer( &m_frameChanged );
    ui->labelHueVsHue->setDiagramType( HueVsDiagram::HueVsHue );
    ui->labelHueVsHue->paintElement();

    //HueVsSat
    ui->labelHueVsSat->setFrameChangedPointer( &m_frameChanged );
    ui->labelHueVsSat->setDiagramType( HueVsDiagram::HueVsSaturation );
    ui->labelHueVsSat->paintElement();

    //HueVsLuma
    ui->labelHueVsLuma->setFrameChangedPointer( &m_frameChanged );
    ui->labelHueVsLuma->setDiagramType( HueVsDiagram::HueVsLuminance );
    ui->labelHueVsLuma->paintElement();

    //LumaVsSat
    ui->labelLumaVsSat->setFrameChangedPointer( &m_frameChanged );
    ui->labelLumaVsSat->setDiagramType( HueVsDiagram::LuminanceVsSaturation );
    ui->labelLumaVsSat->paintElement();

    //Call temp sliders once for stylesheet
    on_horizontalSliderTemperature_valueChanged( ui->horizontalSliderTemperature->value() );
    on_horizontalSliderTint_valueChanged( ui->horizontalSliderTint->value() );

    //WB Picker Mode
    m_wbMode = 0;
    ui->toolButtonWbMode->setToolTip( tr( "Chose between WB picker on grey or on skin" ) );

    //DualIso Button by default invisible
    ui->toolButtonDualIsoForce->setVisible( false );

    //Vidstab
    on_checkBoxVidstabEnable_toggled( false );

    //Sharpen Mask is disabled by default
    ui->label_ShMasking->setEnabled( false );
    ui->label_ShMaskingText->setEnabled( false );
    ui->horizontalSliderShMasking->setEnabled( false );

    //Reveal in Explorer
#ifdef Q_OS_WIN
    ui->actionShowInFinder->setText( tr( "Reveal in Explorer" ) );
    ui->actionShowInFinder->setToolTip( tr( "Reveal selected file in Explorer" ) );
#endif
#ifdef Q_OS_LINUX
    ui->actionShowInFinder->setText( tr( "Reveal in Nautilus" ) );
    ui->actionShowInFinder->setToolTip( tr( "Reveal selected file in Nautilus" ) );
#endif

    //set CPU Usage
    m_countTimeDown = -1;   //Time in seconds for CPU countdown

    //raw2mlv available?
    ui->actionTranscodeAndImport->setVisible( false );
#ifdef Q_OS_WIN
    if( QFileInfo( QString( "%1/raw2mlv.exe" ).arg( QCoreApplication::applicationDirPath() ) ).exists() )
        ui->actionTranscodeAndImport->setVisible( true );
#endif
#ifdef Q_OS_UNIX
    if( QFileInfo( QString( "%1/raw2mlv" ).arg( QCoreApplication::applicationDirPath() ) ).exists() )
        ui->actionTranscodeAndImport->setVisible( true );
#endif
}

//Initialize the library
void MainWindow::initLib( void )
{
    //Get the amount of RAM
    uint32_t maxRam = getMemorySize() / 1024 / 1024;
    /* Limit frame cache to suitable amount of RAM (~33% at 8GB and below, ~50% at 16GB, then up and up) */
    if (maxRam < 7500) m_cacheSizeMB = maxRam * 0.33;
    else m_cacheSizeMB = (uint32_t)(0.66666f * (float)(maxRam - 4000));
    //qDebug() << "Set m_cacheSizeMB to:" << m_cacheSizeMB << "MB of" << maxRam << "MB of total Memory";

    /* Initialise the MLV object so it is actually useful */
    m_pMlvObject = initMlvObject();
    /* Intialise the processing settings object */
    m_pProcessingObject = initProcessingObject();
    /* Set exposure to + 1.2 stops instead of correct 0.0, this is to give the impression
     * (to those that believe) that highlights are recoverable (shhh don't tell) */
    processingSetExposureStops( m_pProcessingObject, 1.2 );
    /* Link video with processing settings */
    setMlvProcessing( m_pMlvObject, m_pProcessingObject );
    processingSetImageProfile(m_pProcessingObject, PROFILE_TONEMAPPED);
    /* Limit frame cache to MAX_RAM size */
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, m_cacheSizeMB );
    /* Use AMaZE */
    setMlvDontAlwaysUseAmaze( m_pMlvObject );
    /* Caching */
    if( ui->actionCaching->isChecked() )
    {
        enableMlvCaching( m_pMlvObject );
    }
    else
    {
        disableMlvCaching( m_pMlvObject );
    }

    m_pRawImage = NULL;
}

//Read some settings from registry
void MainWindow::readSettings()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    restoreGeometry( set.value( "mainWindowGeometry" ).toByteArray() );
    //restoreState( set.value( "mainWindowState" ).toByteArray() ); // create docks, toolbars, etc...
    if( set.value( "dragFrameMode", true ).toBool() ) ui->actionDropFrameMode->setChecked( true );
    if( set.value( "audioOutput", true ).toBool() ) ui->actionAudioOutput->setChecked( true );
    if( set.value( "zebras", false ).toBool() ) ui->actionShowZebras->setChecked( true );
    m_lastExportPath = set.value( "lastExportPath", QDir::homePath() ).toString();
    m_lastMlvOpenFileName = set.value( "lastMlvFileName", QDir::homePath() ).toString();
    m_lastSessionFileName = set.value( "lastSessionFileName", QDir::homePath() ).toString();
    m_lastReceiptFileName = set.value( "lastReceiptFileName", QDir::homePath() ).toString();
    m_lastDarkframeFileName = set.value( "lastDarkframeFileName", QDir::homePath() ).toString();
    m_externalApplicationName = set.value( "externalAppName", QString( "" ) ).toString();
    m_lastLutFileName = set.value( "lastLutFile", QDir::homePath() ).toString();
    m_codecProfile = set.value( "codecProfile", 4 ).toUInt();
    m_codecOption = set.value( "codecOption", 0 ).toUInt();
    m_exportDebayerMode = set.value( "exportDebayerMode", 4 ).toUInt();
    m_previewMode = set.value( "previewMode", 1 ).toUInt();
    switch( m_previewMode )
    {
    case 0:
        ui->actionPreviewDisabled->setChecked( true );
        on_actionPreviewDisabled_triggered();
        break;
    case 1:
        ui->actionPreviewList->setChecked( true );
        on_actionPreviewList_triggered();
        break;
    case 2:
        ui->actionPreviewPicture->setChecked( true );
        on_actionPreviewPicture_triggered();
        break;
    default:
        ui->actionPreviewPictureBottom->setChecked( true );
        on_actionPreviewPictureBottom_triggered();
        break;
    }
    ui->actionCaching->setChecked( false );
    m_resizeFilterEnabled = set.value( "resizeEnable", false ).toBool();
    m_resizeWidth = set.value( "resizeWidth", 1920 ).toUInt();
    m_resizeHeight = set.value( "resizeHeight", 1080 ).toUInt();
    m_resizeFilterHeightLocked = set.value( "resizeLockHeight", false ).toBool();
    m_smoothFilterSetting = set.value( "smoothEnabled", 0 ).toUInt();
    m_hdrExport = set.value( "hdrExport", false ).toBool();
    m_fpsOverride = set.value( "fpsOverride", false ).toBool();
    m_frameRate = set.value( "frameRate", 25 ).toDouble();
    m_audioExportEnabled = set.value( "audioExportEnabled", true ).toBool();
    ui->groupBoxRawCorrection->setChecked( set.value( "expandedRawCorrection", false ).toBool() );
    ui->groupBoxCutInOut->setChecked( set.value( "expandedCutInOut", false ).toBool() );
    ui->groupBoxDebayer->setChecked( set.value( "expandedDebayer", true ).toBool() );
    ui->groupBoxProfiles->setChecked( set.value( "expandedProfiles", true ).toBool() );
    ui->groupBoxProcessing->setChecked( set.value( "expandedProcessing", true ).toBool() );
    ui->groupBoxDetails->setChecked( set.value( "expandedDetails", false ).toBool() );
    ui->groupBoxHsl->setChecked( set.value( "expandedHsl", false ).toBool() );
    ui->groupBoxToning->setChecked( set.value( "expandedToning", false ).toBool() );
    ui->groupBoxColorWheels->setChecked( set.value( "expandedColorWheels", false ).toBool() );
    ui->groupBoxLut->setChecked( set.value( "expandedLut", false ).toBool() );
    ui->groupBoxFilter->setChecked( set.value( "expandedFilter", false ).toBool() );
    ui->groupBoxVignette->setChecked( set.value( "expandedVignette", false ).toBool() );
    ui->groupBoxLinearGradient->setChecked( set.value( "expandedLinGradient", false ).toBool() );
    ui->groupBoxTransformation->setChecked( set.value( "expandedTransformation", false ).toBool() );
    ui->actionCreateMappFiles->setChecked( set.value( "createMappFiles", false ).toBool() );
    m_timeCodePosition = set.value( "tcPos", 1 ).toUInt();
    ui->actionAutoCheckForUpdates->setChecked( set.value( "autoUpdateCheck", true ).toBool() );
    ui->actionPlaybackPosition->setChecked( set.value( "rememberPlaybackPos", false ).toBool() );
    resizeDocks({ui->dockWidgetEdit}, {set.value( "dockEditSize", 212 ).toInt()}, Qt::Horizontal);
    resizeDocks({ui->dockWidgetSession}, {set.value( "dockSessionSize", 170 ).toInt()}, Qt::Horizontal);
    resizeDocks({ui->dockWidgetSession}, {set.value( "dockSessionSize", 130 ).toInt()}, Qt::Vertical);
    m_pRecentFilesMenu->restoreState( set.value("recentSessions").toByteArray() );
    ui->actionAskForSavingOnQuit->setChecked( set.value( "askForSavingOnQuit", true ).toBool() );
    ui->actionBetterResizer->setChecked( set.value( "betterResizerViewer", false ).toBool() );
    int themeId = set.value( "themeId", 0 ).toInt();
    if( themeId == 0 )
    {
        ui->actionDarkThemeStandard->setChecked( true );
        on_actionDarkThemeStandard_triggered( true );
    }
    else
    {
        ui->actionDarkThemeModern->setChecked( true );
        on_actionDarkThemeModern_triggered( true );
    }
}

//Save some settings to registry
void MainWindow::writeSettings()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.setValue( "mainWindowGeometry", saveGeometry() );
    //set.setValue( "mainWindowState", saveState() ); // docks, toolbars, etc...
    set.setValue( "dragFrameMode", ui->actionDropFrameMode->isChecked() );
    set.setValue( "audioOutput", ui->actionAudioOutput->isChecked() );
    set.setValue( "zebras", ui->actionShowZebras->isChecked() );
    set.setValue( "lastExportPath", m_lastExportPath );
    set.setValue( "lastMlvFileName", m_lastMlvOpenFileName );
    set.setValue( "lastSessionFileName", m_lastSessionFileName );
    set.setValue( "lastReceiptFileName", m_lastReceiptFileName );
    set.setValue( "lastDarkframeFileName", m_lastDarkframeFileName );
    set.setValue( "externalAppName", m_externalApplicationName );
    set.setValue( "lastLutFile", m_lastLutFileName );
    set.setValue( "codecProfile", m_codecProfile );
    set.setValue( "codecOption", m_codecOption );
    set.setValue( "exportDebayerMode", m_exportDebayerMode );
    set.setValue( "previewMode", m_previewMode );
    set.setValue( "caching", ui->actionCaching->isChecked() );
    set.setValue( "resizeEnable", m_resizeFilterEnabled );
    set.setValue( "resizeWidth", m_resizeWidth );
    set.setValue( "resizeHeight", m_resizeHeight );
    set.setValue( "resizeLockHeight", m_resizeFilterHeightLocked );
    set.setValue( "smoothEnabled", m_smoothFilterSetting );
    set.setValue( "hdrExport", m_hdrExport );
    set.setValue( "fpsOverride", m_fpsOverride );
    set.setValue( "frameRate", m_frameRate );
    set.setValue( "audioExportEnabled", m_audioExportEnabled );
    set.setValue( "expandedRawCorrection", ui->groupBoxRawCorrection->isChecked() );
    set.setValue( "expandedCutInOut", ui->groupBoxCutInOut->isChecked() );
    set.setValue( "expandedDebayer", ui->groupBoxDebayer->isChecked() );
    set.setValue( "expandedProfiles", ui->groupBoxProfiles->isChecked() );
    set.setValue( "expandedProcessing", ui->groupBoxProcessing->isChecked() );
    set.setValue( "expandedDetails", ui->groupBoxDetails->isChecked() );
    set.setValue( "expandedHsl", ui->groupBoxHsl->isChecked() );
    set.setValue( "expandedToning", ui->groupBoxToning->isChecked() );
    set.setValue( "expandedColorWheels", ui->groupBoxColorWheels->isChecked() );
    set.setValue( "expandedLut", ui->groupBoxLut->isChecked() );
    set.setValue( "expandedFilter", ui->groupBoxFilter->isChecked() );
    set.setValue( "expandedVignette", ui->groupBoxVignette->isChecked() );
    set.setValue( "expandedLinGradient", ui->groupBoxLinearGradient->isChecked() );
    set.setValue( "expandedTransformation", ui->groupBoxTransformation->isChecked() );
    set.setValue( "createMappFiles", ui->actionCreateMappFiles->isChecked() );
    set.setValue( "tcPos", m_timeCodePosition );
    set.setValue( "autoUpdateCheck", ui->actionAutoCheckForUpdates->isChecked() );
    set.setValue( "rememberPlaybackPos", ui->actionPlaybackPosition->isChecked() );
    set.setValue( "dockEditSize", ui->dockWidgetEdit->width() );
    if( m_previewMode == 3 ) set.setValue( "dockSessionSize", ui->dockWidgetSession->height() );
    else set.setValue( "dockSessionSize", ui->dockWidgetSession->width() );
    set.setValue( "recentSessions", m_pRecentFilesMenu->saveState() );
    set.setValue( "askForSavingOnQuit", ui->actionAskForSavingOnQuit->isChecked() );
    set.setValue( "betterResizerViewer", ui->actionBetterResizer->isChecked() );
    if( ui->actionDarkThemeStandard->isChecked() ) set.setValue( "themeId", 0 );
    else set.setValue( "themeId", 1 );
}

//Start Export via Pipe
void MainWindow::startExportPipe(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    //chose if we want to get amaze frames for exporting, or bilinear
    if( m_exportDebayerMode == 0 )
    {
        setMlvDontAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 1 )
    {
        setMlvAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 2 )
    {
        setMlvUseLmmseDebayer( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 3 )
    {
        setMlvUseIgvDebayer( m_pMlvObject );
    }
    else
    {
        switch( m_exportQueue.first()->debayer() )
        {
        case ReceiptSettings::None:
            setMlvUseNoneDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Simple:
            setMlvUseSimpleDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Bilinear:
            setMlvDontAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::LMMSE:
            setMlvUseLmmseDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::IGV:
            setMlvUseIgvDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::AMaZE:
            setMlvAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::AHD:
            setMlvUseAhdDebayer( m_pMlvObject );
            break;
        default:
            break;
        }
    }
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_pMlvObject->current_cached_frame_active = 0;
    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();

    //Audio Export
    QString wavFileName = QString( "%1.wav" ).arg( fileName.left( fileName.lastIndexOf( "." ) ) );
    QString ffmpegAudioCommand;
    ffmpegAudioCommand.clear();
    if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
    {
#ifdef Q_OS_UNIX
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toUtf8().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#else
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toLatin1().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#endif
        if( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265 ) ffmpegAudioCommand = QString( "-i \"%1\" -c:a aac " ).arg( wavFileName );
        else ffmpegAudioCommand = QString( "-i \"%1\" -c:a copy " ).arg( wavFileName );
    }

    //If audio only, exit here
    if( m_codecProfile == CODEC_AUDIO_ONLY )
    {
        //Set Status
        m_pStatusDialog->ui->progressBar->setValue( (m_exportQueue.first()->cutOut() - 1) - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
        m_pStatusDialog->ui->progressBar->repaint();
        qApp->processEvents();

        if( !doesMlvHaveAudio( m_pMlvObject ) )
        {
            //Hide Status Dialog
            m_pStatusDialog->close();
            qApp->processEvents();
            //Then show error
            int ret = QMessageBox::critical( this,
                                             tr( "MLV App - Export file error" ),
                                             tr( "No audio track available in MLV for export.\nHow do you like to proceed?" ),
                                             tr( "Continue" ),
                                             tr( "Abort batch export" ),
                                             0, 0 );
            if( ret == 1 )
            {
                exportAbort();
            }
        }

        //Delete wav file if aborted
        if( m_exportAbortPressed )
        {
            QFile *file = new QFile( wavFileName );
            if( file->exists() ) file->remove();
            delete file;
        }

        //If we don't like amaze we switch it off again
        if( !ui->actionAlwaysUseAMaZE->isChecked() ) setMlvDontAlwaysUseAmaze( m_pMlvObject );

        //Enable GUI drawing
        m_dontDraw = false;

        //Emit Ready-Signal
        emit exportReady();
        return;
    }

    //HDR detection check
    bool isHdrClip = false;
    if( m_hdrExport && ( getMlvFrames( m_pMlvObject ) >= 2 ) )
    {
        //Buffer
        uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
        uint16_t * imgBuffer;
        imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );

        //Take 2 pics from the middle of the clip
        uint8_t frameNum = getMlvFrames( m_pMlvObject ) / 2 - 1;

        //Get 1st picture, and lock render thread... there can only be one!
        m_pRenderThread->lock();
        getMlvProcessedFrame16( m_pMlvObject, frameNum, imgBuffer, QThread::idealThreadCount() );
        m_pRenderThread->unlock();
        double average1 = 0;
        for( uint32_t i = 0; i < frameSize; i++ ) average1 += imgBuffer[i];

        //Get 2nd picture, and lock render thread... there can only be one!
        m_pRenderThread->lock();
        getMlvProcessedFrame16( m_pMlvObject, frameNum+1, imgBuffer, QThread::idealThreadCount() );
        m_pRenderThread->unlock();
        double average2 = 0;
        for( uint32_t i = 0; i < frameSize; i++ ) average2 += imgBuffer[i];

        //Compare pictures
        if( average2 == 0 ) average2 = 1;
        double quot = average1 / average2;
        if( quot > 1.3 || quot < 0.7 ) isHdrClip = true;
        //qDebug() << average1 << average2 << quot;

        free( imgBuffer );
    }

    //Solving the . and , problem at fps in the command
    QLocale locale = QLocale(QLocale::English, QLocale::UnitedKingdom);
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QString fps = locale.toString( getFramerate() );

    //Doing something against moiree
    QString moireeFilter = QString( "" );
    if( m_smoothFilterSetting != SMOOTH_FILTER_OFF )
    {
        //minterpolate, tblend and framestep are filters. The 1st does the oversampling.
        //The 2nd, the blended frames, and 3rd reduces the stream back to original fps.
        moireeFilter = QString( "minterpolate=%1,tblend=all_mode=average,framestep=2," )
                .arg( locale.toString( getFramerate() * 2.0 ) );
        if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM )
        {
            moireeFilter.append( QString( "unsharp=7:7:0.8:7:7:0," ) );
        }
        else if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM_BB )
        {
            moireeFilter.append( QString( "unsharp=5:5:0.6:5:5:0," ) );
        }
    }

    //HDR and blending
    QString hdrString = QString( "" );
    if( m_hdrExport && isHdrClip )
        hdrString = QString( ",tblend=all_mode=average" );

    if( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_AVG )
    {
        int frames = m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1;
        if( frames > 128 ) frames = 128;
        hdrString = QString( ",tmix=frames=%1" ).arg( frames );
    }

    //Vidstab, 2nd pass
    QString vidstabString = QString( "" );
#ifdef Q_OS_WIN
    QString vidstabFile = QString( "\"tmp_transform_vectors.trf\"" );
#elif defined( Q_OS_LINUX )
    QString vidstabFile = QString( "\"%1/tmp_transform_vectors.trf\"" )
            .arg( QFileInfo( m_exportQueue.first()->fileName() ).absolutePath() );
#else
    QString vidstabFile = QString( "\"%1/tmp_transform_vectors.trf\"" ).arg( QCoreApplication::applicationDirPath() );
#endif
    if( m_exportQueue.first()->vidStabEnabled() && m_codecProfile == CODEC_H264 )
    {
        if( m_exportQueue.first()->vidStabTripod() )
        {
            vidstabString = QString( ",vidstabtransform=input=%1:tripod=1" )
                .arg( vidstabFile );
        }
        else
        {
            vidstabString = QString( ",vidstabtransform=input=%1:zoom=%2:smoothing=%3" )
                .arg( vidstabFile )
                .arg( m_exportQueue.first()->vidStabZoom() )
                .arg( m_exportQueue.first()->vidStabSmoothing() );
        }
    }

    //Colorspace conversion (for getting right colors)
    QString resizeFilter = QString( "" );
    //a colorspace conversion is always needed to get right colors
    resizeFilter = QString( "-vf %1scale=in_color_matrix=bt601:out_color_matrix=bt709%2%3 " )
            .arg( moireeFilter )
            .arg( hdrString )
            .arg( vidstabString );
    //qDebug() << resizeFilter;

    //Dimension & scaling
    uint16_t width = getMlvWidth(m_pMlvObject);
    uint16_t height = getMlvHeight(m_pMlvObject);
    bool scaled = false;
    if( m_resizeFilterEnabled )
    {
        //Autocalc height
        if( m_resizeFilterHeightLocked )
        {
            height = (double)m_resizeWidth / (double)getMlvWidth( m_pMlvObject )
                    / m_exportQueue.first()->stretchFactorX()
                    * m_exportQueue.first()->stretchFactorY()
                    * (double)getMlvHeight( m_pMlvObject ) + 0.5;
        }
        else
        {
            height = m_resizeHeight;
        }
        width = m_resizeWidth;
        scaled = true;
    }
    else if( m_exportQueue.first()->stretchFactorX() != 1.0
          || m_exportQueue.first()->stretchFactorY() != 1.0 )
    {
        //Upscale only
        if( m_exportQueue.first()->stretchFactorY() == STRETCH_V_033 )
        {
            width = getMlvWidth( m_pMlvObject ) * 3;
            height = getMlvHeight( m_pMlvObject );
        }
        else
        {
            width = getMlvWidth( m_pMlvObject ) * m_exportQueue.first()->stretchFactorX();
            height = getMlvHeight( m_pMlvObject ) * m_exportQueue.first()->stretchFactorY();
        }
        scaled = true;
    }
    if( m_codecProfile == CODEC_H264
     || m_codecProfile == CODEC_H265 )
    {
        if( width != width + (width % 2) )
        {
            width += width % 2;
            scaled = true;
        }
        if( height != height + (height % 2) )
        {
            height += height % 2;
            scaled = true;
        }
    }

    //FFMpeg export
#if defined __linux__ && !defined APP_IMAGE
    QString program = QString( "ffmpeg" );
#elif __WIN32__
    QString program = QString( "ffmpeg" );
#else
    QString program = QCoreApplication::applicationDirPath();
    program.append( QString( "/ffmpeg\"" ) );
    program.prepend( QString( "\"" ) );
#endif

#ifdef STDOUT_SILENT
    program.append( QString( " -loglevel 0" ) );
#endif

    //We need it later for multipass
    QString ffmpegCommand = program;

    QString output = fileName.left( fileName.lastIndexOf( "." ) );
    QString resolution = QString( "%1x%2" ).arg( width ).arg( height );

    //VidStab: First pass
    if( m_exportQueue.first()->vidStabEnabled() && m_codecProfile == CODEC_H264 )
    {
        QString stabCmd;
        if( m_exportQueue.first()->vidStabTripod() )
        {
            stabCmd = QString( "%1 -r %2 -y -f rawvideo -s %3 -pix_fmt rgb48 -i - -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf vidstabdetect=tripod=1:result=%4 -f null -" )
                        .arg( program )
                        .arg( fps )
                        .arg( resolution )
                        .arg( vidstabFile );
        }
        else
        {
            stabCmd = QString( "%1 -r %2 -y -f rawvideo -s %3 -pix_fmt rgb48 -i - -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf vidstabdetect=stepsize=%5:shakiness=%6:accuracy=%7:result=%4 -f null -" )
                        .arg( program )
                        .arg( fps )
                        .arg( resolution )
                        .arg( vidstabFile )
                        .arg( m_exportQueue.first()->vidStabStepsize() )
                        .arg( m_exportQueue.first()->vidStabShakiness() )
                        .arg( m_exportQueue.first()->vidStabAccuracy() );
        }

        //Try to open pipe
        FILE *pPipeStab;
        //qDebug() << "Call ffmpeg:" << stabCmd;
    #ifdef Q_OS_UNIX
        if( !( pPipeStab = popen( stabCmd.toUtf8().data(), "w" ) ) )
    #else
        if( !( pPipeStab = popen( stabCmd.toLatin1().data(), "wb" ) ) )
    #endif
        {
            QMessageBox::critical( this, tr( "File export failed" ), tr( "Could not export with ffmpeg." ) );
        }
        else
        {
            //Buffer
            uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
            uint16_t * imgBuffer;
            imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );

            //Frames in the export queue?!
            int totalFrames = 0;
            for( int i = 0; i < m_exportQueue.count(); i++ )
            {
                totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
            }

            //Build buffer
            uint16_t * imgBufferScaled;
            imgBufferScaled = ( uint16_t* )malloc( width * height * 3 * sizeof( uint16_t ) );

            //Get all pictures and send to pipe
            for( uint32_t i = (m_exportQueue.first()->cutIn() - 1); i < m_exportQueue.first()->cutOut(); i++ )
            {
                if( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_AVG && i > 128 ) break;

                if( scaled )
                {
                    //Get picture, and lock render thread... there can only be one!
                    m_pRenderThread->lock();
                    getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                    m_pRenderThread->unlock();

                    avir_scale_thread_pool scaling_pool;
                    avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                    avir::CImageResizerParamsUltra roptions;
                    avir::CImageResizer<> image_resizer( 16, 0, roptions );
                    image_resizer.resizeImage( imgBuffer,
                                               getMlvWidth(m_pMlvObject),
                                               getMlvHeight(m_pMlvObject), 0,
                                               imgBufferScaled,
                                               width,
                                               height,
                                               3, 0, &vars );

                    //Write to pipe
                    fwrite(imgBufferScaled, sizeof( uint16_t ), width * height * 3, pPipeStab);
                    fflush(pPipeStab);
                }
                else
                {
                    //Get picture, and lock render thread... there can only be one!
                    m_pRenderThread->lock();
                    getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                    m_pRenderThread->unlock();

                    //Write to pipe
                    fwrite(imgBuffer, sizeof( uint16_t ), frameSize, pPipeStab);
                    fflush(pPipeStab);
                }

                //Set Status
                m_pStatusDialog->ui->progressBar->setValue( ( i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 );
                m_pStatusDialog->ui->progressBar->repaint();
                m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - ( ( i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 ) );
                qApp->processEvents();

                //Abort pressed? -> End the loop
                if( m_exportAbortPressed ) break;
            }
            //Close pipe
            pclose( pPipeStab );
            free( imgBufferScaled );
            free( imgBuffer );
        }
    }

    if( m_codecProfile == CODEC_TIFF )
    {
        if( m_codecOption == CODEC_TIFF_SEQ )
        {
            //Creating a folder with the initial filename
            QString folderName = QFileInfo( fileName ).path();
            QString shortFileName = QFileInfo( fileName ).fileName();
            folderName.append( "/" )
                    .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) );

            QDir dir;
            dir.mkpath( folderName );

            //Now add the numbered filename
            output = folderName;
            output.append( "/" )
                    .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) )
                    .append( QString( "_%06d.tif" ) );

            program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v tiff -pix_fmt %3 -start_number %4 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %5\"%6\"" )
                        .arg( fps )
                        .arg( resolution )
                        .arg( "rgb48" )
                        .arg( m_exportQueue.first()->cutIn() - 1 )
                        .arg( resizeFilter )
                        .arg( output ) );

            //copy wav to the location, ffmpeg does not like to do it for us :-(
            if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
            {
                QFile::copy( wavFileName, QString( "%1/%2.wav" ).arg( folderName ).arg( shortFileName.left( shortFileName.lastIndexOf( "." ) ) ) );
            }
            //Setup for scripting
            m_pScripting->setNextScriptInputTiff( getMlvFramerate( m_pMlvObject ), folderName );
        }
        else
        {
            output.append( QString( ".tif" ) );
            program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v tiff -pix_fmt %3 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %4\"%5\"" )
                        .arg( fps )
                        .arg( resolution )
                        .arg( "rgb48" )
                        .arg( resizeFilter )
                        .arg( output ) );
        }
    }
    else if( m_codecProfile == CODEC_PNG )
    {
        //Creating a folder with the initial filename
        QString folderName = QFileInfo( fileName ).path();
        QString shortFileName = QFileInfo( fileName ).fileName();
        folderName.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) );

        QDir dir;
        dir.mkpath( folderName );

        QString pngDepth;
        if( m_codecOption == CODEC_PNG_16 ) pngDepth = "rgb48";
        else pngDepth = "rgb24";

        //Now add the numbered filename
        output = folderName;
        output.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) )
                .append( QString( "_%06d.png" ) );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v png -pix_fmt %3 -start_number %4 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( pngDepth )
                    .arg( m_exportQueue.first()->cutIn() - 1 )
                    .arg( resizeFilter )
                    .arg( output ) );

        //copy wav to the location, ffmpeg does not like to do it for us :-(
        if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
        {
            QFile::copy( wavFileName, QString( "%1/%2.wav" ).arg( folderName ).arg( shortFileName.left( shortFileName.lastIndexOf( "." ) ) ) );
        }
        //Setup for scripting
        m_pScripting->setNextScriptInputTiff( getMlvFramerate( m_pMlvObject ), folderName );
    }
    else if( m_codecProfile == CODEC_JPG2K && m_codecOption == CODEC_JPG2K_SEQ )
    {
        //Creating a folder with the initial filename
        QString folderName = QFileInfo( fileName ).path();
        QString shortFileName = QFileInfo( fileName ).fileName();
        folderName.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) );

        QDir dir;
        dir.mkpath( folderName );

        //Now add the numbered filename
        output = folderName;
        output.append( "/" )
                .append( shortFileName.left( shortFileName.lastIndexOf( "." ) ) )
                .append( QString( "_%06d.jp2" ) );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v jpeg2000 -pix_fmt %3 -start_number %4 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "yuv444p" )
                    .arg( m_exportQueue.first()->cutIn() - 1 )
                    .arg( resizeFilter )
                    .arg( output ) );

        //copy wav to the location, ffmpeg does not like to do it for us :-(
        if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) )
        {
            QFile::copy( wavFileName, QString( "%1/%2.wav" ).arg( folderName ).arg( shortFileName.left( shortFileName.lastIndexOf( "." ) ) ) );
        }
        //Setup for scripting
        m_pScripting->setNextScriptInputTiff( getMlvFramerate( m_pMlvObject ), folderName );
    }
    else if( m_codecProfile == CODEC_JPG2K && m_codecOption == CODEC_JPG2K_MOV )
    {
        output.append( QString( ".mov" ) );

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v jpeg2000 -pix_fmt %3 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %4\"%5\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "yuv444p" )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_AVI )
    {
        output.append( QString( ".avi" ) );

        QString option3;
        QString option4;

        switch( m_codecOption )
        {
            case CODEC_AVI_OPTION_YUV420:
                option3 = "rawvideo";
                option4 = "yuv420p";
                break;
            case CODEC_AVI_OPTION_V210:
                option3 = "v210";
                option4 = "yuv422p10le";
                break;
             case CODEC_AVI_OPTION_BGR24:
                option3 = "rawvideo";
                option4 = "bgr24";
                break;
        }

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -pix_fmt %4 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option3 )
                    .arg( option4 )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_MJPEG )
    {
        output.append( QString( ".avi" ) );
        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -pix_fmt %4 -q:v 2 -huffman optimal -an -vtag MJPG %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "mjpeg" )
                    .arg( "yuvj444p" )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_FFVHUFF )
    {
        output.append( QString( ".avi" ) );

        QString option;

        switch( m_codecOption )
        {
            case CODEC_FFVHUFF_OPTION10:
                option = "yuv444p10le";
                break;
            case CODEC_FFVHUFF_OPTION12:
                option = "yuv444p12le";
                break;
            default: //16bit
                option = "yuv444p16le";
                break;
        }

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -pix_fmt %4 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( "ffvhuff" )
                    .arg( option )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_H264 )
    {
        if( m_codecOption == CODEC_H264_H_MOV || m_codecOption == CODEC_H264_M_MOV ) output.append( QString( ".mov" ) );
        else if( m_codecOption == CODEC_H264_H_MP4 || m_codecOption == CODEC_H264_M_MP4 ) output.append( QString( ".mp4" ) );
        else output.append( QString( ".mkv" ) );

        int quality;
        if( m_codecOption == CODEC_H264_H_MOV || m_codecOption == CODEC_H264_H_MP4 || m_codecOption == CODEC_H264_H_MKV )
            quality = 14;
        else
            quality = 24;

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v libx264 -preset medium -crf %3 -pix_fmt %4 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( quality )
                    .arg( "yuv420p" )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_H265 )
    {
        if( m_codecOption == CODEC_H265_H_MOV || m_codecOption == CODEC_H265_M_MOV ) output.append( QString( ".mov" ) );
        else if( m_codecOption == CODEC_H265_H_MP4 || m_codecOption == CODEC_H265_M_MP4 ) output.append( QString( ".mp4" ) );
        else output.append( QString( ".mkv" ) );

        int quality;
        if( m_codecOption == CODEC_H265_H_MOV || m_codecOption == CODEC_H265_H_MP4 || m_codecOption == CODEC_H265_H_MKV )
            quality = 18;
        else
            quality = 24;

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v libx265 -preset medium -crf %3 -pix_fmt %4 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %5\"%6\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( quality )
                    .arg( "yuv420p" )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    else if( m_codecProfile == CODEC_DNXHD
          || m_codecProfile == CODEC_DNXHR )
    {
        output.append( QString( ".mov" ) );

        QString option;
        QString option2;
        QString format;

        if( m_codecProfile == CODEC_DNXHD )
        {
            format = "format=yuv422p10";
            option2 = "";
        }
        else
        {
            switch( m_codecOption )
            {
            case CODEC_DNXHR_444_1080p_10bit:
                format = "format=yuv444p10";
                option2 = "-profile:v dnxhr_444 ";
                break;
            case CODEC_DNXHR_HQX_1080p_10bit:
                format = "format=yuv422p10";
                option2 = "-profile:v dnxhr_hqx ";
                break;
            case CODEC_DNXHR_HQ_1080p_8bit:
                format = "format=yuv422p";
                option2 = "-profile:v dnxhr_hq ";
                break;
            case CODEC_DNXHR_SQ_1080p_8bit:
                format = "format=yuv422p";
                option2 = "-profile:v dnxhr_sq ";
                break;
            case CODEC_DNXHR_LB_1080p_8bit:
            default:
                format = "format=yuv422p";
                option2 = "-profile:v dnxhr_lb ";
                break;
            }
        }

        bool error = false;

        if( ( ( m_codecProfile == CODEC_DNXHD ) && ( m_codecOption == CODEC_DNXHD_1080p_10bit ) )
         || m_codecProfile == CODEC_DNXHR )
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,%1%2 -b:v 185M" ).arg( format ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,%1%2 -b:v 365M" ).arg( format ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,%1%2 -b:v 175M" ).arg( format ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,%1%2 -b:v 220M" ).arg( format ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,%1%2 -b:v 440M" ).arg( format ).arg( hdrString );
            else error = true;
        }
        else if( m_codecOption == CODEC_DNXHD_1080p_8bit )
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,format=yuv422p%1 -b:v 185M" ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,format=yuv422p%1 -b:v 365M" ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,format=yuv422p%1 -b:v 175M" ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,format=yuv422p%1 -b:v 220M" ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1920:h=1080:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,format=yuv422p%1 -b:v 440M" ).arg( hdrString );
            else error = true;
        }
        else if( m_codecOption == CODEC_DNXHD_720p_10bit )
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,format=yuv422p10%1 -b:v 90M" ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,format=yuv422p10%1 -b:v 180M" ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,format=yuv422p10%1 -b:v 90M" ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,format=yuv422p10%1 -b:v 110M" ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,format=yuv422p10%1 -b:v 220M" ).arg( hdrString );
            else error = true;
        }
        else //720p 8bit
        {
            if( getFramerate() == 25.0 )                option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=25,format=yuv422p%1 -b:v 90M" ).arg( hdrString );
            else if( getFramerate() == 50.0 )           option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=50,format=yuv422p%1 -b:v 180M" ).arg( hdrString );
            else if( fps == QString( "23.976" ) || fps == QString( "23,976" )
                     || getFramerate() == 24000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=24000/1001,format=yuv422p%1 -b:v 90M" ).arg( hdrString );
            else if( fps == QString( "29.97" ) || fps == QString( "29,97" )
                     || getFramerate() == 30000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=30000/1001,format=yuv422p%1 -b:v 110M" ).arg( hdrString );
            else if( fps == QString( "59.94" ) || fps == QString( "59,94" )
                     || getFramerate() == 60000.0/1001.0 ) option = QString( "-vf scale=w=1280:h=720:in_color_matrix=bt601:out_color_matrix=bt709,fps=60000/1001,format=yuv422p%1 -b:v 220M" ).arg( hdrString );
            else error = true;
        }

        if( error )
        {
            QMessageBox::critical( this, tr( "File export failed" ), tr( "Unsupported framerate!" ) );
            //Emit Ready-Signal
            emit exportReady();
            return;
        }

        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v dnxhd %3%4 -color_primaries bt709 -color_trc bt709 -colorspace bt709 \"%5\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option2 )
                    .arg( option )
                    .arg( output ) );
    }
    else
    {
        QString option;
        if( m_codecProfile <= CODEC_PRORES422HQ && m_codecOption == CODEC_PRORES_OPTION_AW ) option = QString( "prores_aw" );
        else option = QString( "prores_ks" );
        QString pixFmt;
        if( m_codecProfile <= CODEC_PRORES422HQ ) pixFmt = QString( "yuv422p10" );
        else pixFmt = QString( "yuv444p10" );

        output.append( QString( ".mov" ) );
        program.append( QString( " -r %1 -y -f rawvideo -s %2 -pix_fmt rgb48 -i - -c:v %3 -profile:v %4 -pix_fmt %5 -color_primaries bt709 -color_trc bt709 -colorspace bt709 %6\"%7\"" )
                    .arg( fps )
                    .arg( resolution )
                    .arg( option )
                    .arg( m_codecProfile )
                    .arg( pixFmt )
                    .arg( resizeFilter )
                    .arg( output ) );
    }
    //There is a %5 in the string, so another arg is not possible - so do that:
    program.insert( program.indexOf( "-c:v" ), ffmpegAudioCommand );

    //Do 3pass filtering!
    if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS || m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM )
    {
        QString pass3 = QString( "-vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -y -i - " ).arg( ffmpegCommand ).arg( locale.toString( getFramerate() * 2.0 ) );
        program.insert( program.indexOf( "-c:v" ), pass3 );
    }
    //Plus box blur
    else if( m_smoothFilterSetting == SMOOTH_FILTER_3PASS_USM_BB )
    {
        QString pass3 = QString( "-filter_complex \"[0:v] boxblur=1:cr=5:ar=5 [tmp]; [0:v][tmp] blend=all_mode='normal':all_opacity=0.7\" -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -i - -vf minterpolate=%2,tblend=all_mode=average,framestep=2 -c:v libx264 -preset ultrafast -crf 10 -f matroska - | %1 -y -i - " ).arg( ffmpegCommand ).arg( locale.toString( getFramerate() * 2.0 ) );
        program.insert( program.indexOf( "-c:v" ), pass3 );
    }

    //Try to open pipe
    FILE *pPipe;
    //qDebug() << "Call ffmpeg:" << program;
#ifdef Q_OS_UNIX
    if( !( pPipe = popen( program.toUtf8().data(), "w" ) ) )
#else
    if( !( pPipe = popen( program.toLatin1().data(), "wb" ) ) )
#endif
    {
        QMessageBox::critical( this, tr( "File export failed" ), tr( "Could not export with ffmpeg." ) );
    }
    else
    {
        //Buffer
        uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
        uint16_t * imgBuffer;
        imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );

        //Frames in the export queue?!
        int totalFrames = 0;
        for( int i = 0; i < m_exportQueue.count(); i++ )
        {
            totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
        }

        //Build buffer
        uint16_t * imgBufferScaled;
        imgBufferScaled = ( uint16_t* )malloc( width * height * 3 * sizeof( uint16_t ) );

        //Get all pictures and send to pipe
        for( uint32_t i = (m_exportQueue.first()->cutIn() - 1); i < m_exportQueue.first()->cutOut(); i++ )
        {
            if( m_codecProfile == CODEC_TIFF && m_codecOption == CODEC_TIFF_AVG && i > 128 ) break;

            if( scaled )
            {
                //Get picture, and lock render thread... there can only be one!
                m_pRenderThread->lock();
                getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                m_pRenderThread->unlock();

                avir_scale_thread_pool scaling_pool;
                avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                avir::CImageResizerParamsUltra roptions;
                avir::CImageResizer<> image_resizer( 16, 0, roptions );
                image_resizer.resizeImage( imgBuffer,
                                           getMlvWidth(m_pMlvObject),
                                           getMlvHeight(m_pMlvObject), 0,
                                           imgBufferScaled,
                                           width,
                                           height,
                                           3, 0, &vars );

                //Write to pipe
                fwrite(imgBufferScaled, sizeof( uint16_t ), width * height * 3, pPipe);
                fflush(pPipe);
            }
            else
            {
                //Get picture, and lock render thread... there can only be one!
                m_pRenderThread->lock();
                getMlvProcessedFrame16( m_pMlvObject, i, imgBuffer, QThread::idealThreadCount() );
                m_pRenderThread->unlock();

                //Write to pipe
                fwrite(imgBuffer, sizeof( uint16_t ), frameSize, pPipe);
                fflush(pPipe);
            }

            //Set Status
            if( !( m_exportQueue.first()->vidStabEnabled() && m_codecProfile == CODEC_H264 ) )
            {
                m_pStatusDialog->ui->progressBar->setValue( i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
                m_pStatusDialog->ui->progressBar->repaint();
                m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - i + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
            }
            else
            {
                m_pStatusDialog->ui->progressBar->setValue( ( totalFrames + i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 );
                m_pStatusDialog->ui->progressBar->repaint();
                m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - ( ( totalFrames + i - ( m_exportQueue.first()->cutIn() - 1 ) + 1 ) >> 1 ) );
            }
            qApp->processEvents();

            //Abort pressed? -> End the loop
            if( m_exportAbortPressed ) break;
        }
        //Close pipe
        pclose( pPipe );
        free( imgBufferScaled );
        free( imgBuffer );
    }

    //Delete wav file
    QFile *file = new QFile( wavFileName );
    if( file->exists() ) file->remove();
    delete file;

    //Delete tmp vidstab file
    vidstabFile.replace( "\"", "" );
    file = new QFile( vidstabFile );
    if( file->exists() ) file->remove();
    delete file;

    //Delete file if aborted
    /*if( m_exportAbortPressed )
    {
        file = new QFile( fileName );
        if( file->exists() ) file->remove();
        delete file;
    }*/

    //If we don't like amaze we switch it off again
    if( !ui->actionAlwaysUseAMaZE->isChecked() ) setMlvDontAlwaysUseAmaze( m_pMlvObject );

    //Enable GUI drawing
    m_dontDraw = false;

    //Emit Ready-Signal
    emit exportReady();
}

//CDNG output
void MainWindow::startExportCdng(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    // we always get amaze frames for exporting
    setMlvAlwaysUseAmaze( m_pMlvObject );
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_pMlvObject->current_cached_frame_active = 0;
    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();
    //Frames in the export queue?!
    int totalFrames = 0;
    for( int i = 0; i < m_exportQueue.count(); i++ )
    {
        totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
    }

    //Create folders and build name schemes
    QString pathName = QFileInfo( fileName ).path();
    fileName = QFileInfo( fileName ).fileName();
    fileName = fileName.left( fileName.indexOf( '.' ) );

    if( m_codecOption == CODEC_CNDG_DEFAULT ) pathName = pathName.append( "/%1" ).arg( fileName );
    else pathName = pathName.append( "/%1_1_%2-%3-%4_0001_C0000" )
            .arg( fileName )
            .arg( getMlvTmYear( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmMonth( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmDay( m_pMlvObject ), 2, 10, QChar('0') );

    //qDebug() << pathName << fileName;
    //Create folder
    QDir dir;
    dir.mkpath( pathName );

    //Output WAVE
    if( doesMlvHaveAudio( m_pMlvObject ) && m_audioExportEnabled )
    {
        QString wavFileName = pathName;
        if( m_codecOption == CODEC_CNDG_DEFAULT ) wavFileName = wavFileName.append( "/%1.wav" ).arg( fileName );
        else wavFileName = wavFileName.append( "/%1_1_%2-%3-%4_0001_C0000.wav" )
            .arg( fileName )
            .arg( getMlvTmYear( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmMonth( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmDay( m_pMlvObject ), 2, 10, QChar('0') );
        //qDebug() << wavFileName;
#ifdef Q_OS_UNIX
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toUtf8().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#else
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toLatin1().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );
#endif
    }

    //Set aspect ratio of the picture
    int32_t picAR[4] = { 0 };
    //Set horizontal stretch
    if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_133 )
    {
        picAR[0] = 4; picAR[1] = 3;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_150 )
    {
        picAR[0] = 3; picAR[1] = 2;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_175 )
    {
        picAR[0] = 7; picAR[1] = 4;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_180 )
    {
        picAR[0] = 9; picAR[1] = 5;
    }
    else if( m_exportQueue.first()->stretchFactorX() == STRETCH_H_200 )
    {
        picAR[0] = 2; picAR[1] = 1;
    }
    else
    {
        picAR[0] = 1; picAR[1] = 1;
    }
    //Set vertical stretch
    if(m_exportQueue.first()->stretchFactorY() == STRETCH_V_167)
    {
        picAR[2] = 5; picAR[3] = 3;
    }
    else if(m_exportQueue.first()->stretchFactorY() == STRETCH_V_300)
    {
        picAR[2] = 3; picAR[3] = 1;
    }
    else if(m_exportQueue.first()->stretchFactorY() == STRETCH_V_033)
    {
        picAR[2] = 1; picAR[3] = 1; picAR[0] *= 3; //Upscale only
    }
    else
    {
        picAR[2] = 1; picAR[3] = 1;
    }

    //Init DNG data struct
    dngObject_t * cinemaDng = initDngObject( m_pMlvObject, m_codecProfile - 6, getFramerate(), picAR);

    //Output frames loop
    for( uint32_t frame = m_exportQueue.first()->cutIn() - 1; frame < m_exportQueue.first()->cutOut(); frame++ )
    {
        QString dngName;
        if( m_codecOption == CODEC_CNDG_DEFAULT ) dngName = dngName.append( "%1_%2.dng" )
                                                                                .arg( fileName )
                                                                                .arg( getMlvFrameNumber( m_pMlvObject, frame ), 6, 10, QChar('0') );
        else dngName = dngName.append( "%1_1_%2-%3-%4_0001_C0000_%5.dng" )
            .arg( fileName )
            .arg( getMlvTmYear( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmMonth( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvTmDay( m_pMlvObject ), 2, 10, QChar('0') )
            .arg( getMlvFrameNumber( m_pMlvObject, frame ), 6, 10, QChar('0') );

        QString filePathNr = pathName;
        filePathNr = filePathNr.append( "/" + dngName );

        //Save cDNG frame
#ifdef Q_OS_UNIX
        if( saveDngFrame( m_pMlvObject, cinemaDng, frame, filePathNr.toUtf8().data() ) )
#else
        if( saveDngFrame( m_pMlvObject, cinemaDng, frame, filePathNr.toLatin1().data() ) )
#endif
        {
            m_pStatusDialog->close();
            qApp->processEvents();
            int ret = QMessageBox::critical( this,
                                             tr( "MLV App - Export file error" ),
                                             tr( "Could not save: %1\nHow do you like to proceed?" ).arg( dngName ),
                                             tr( "Skip frame" ),
                                             tr( "Abort current export" ),
                                             tr( "Abort batch export" ),
                                             0, 2 );
            if( ret == 2 )
            {
                exportAbort();
            }
            if( ret > 0 )
            {
                break;
            }
        }

        //Set Status
        m_pStatusDialog->ui->progressBar->setValue( frame - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
        m_pStatusDialog->ui->progressBar->repaint();
        m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - frame + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
        qApp->processEvents();

        //Abort pressed? -> End the loop
        if( m_exportAbortPressed ) break;
    }

    //Free DNG data struct
    freeDngObject( cinemaDng );

    //Enable GUI drawing
    m_dontDraw = false;

    //Emit Ready-Signal
    emit exportReady();
}

//MLV export
void MainWindow::startExportMlv(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();
    //Frames in the export queue?!
    uint32_t totalFrames = 0;
    for( int i = 0; i < m_exportQueue.count(); i++ )
    {
        totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
    }

    //Create folders and build name schemes
    QString pathName = QFileInfo( fileName ).path();
    fileName = QFileInfo( fileName ).fileName();
    //fileName = fileName.left( fileName.indexOf( '.' ) );
    pathName = pathName.append( "/%1" ).arg( fileName );

    /* open .MLV file for writing */
#ifdef Q_OS_UNIX
    FILE* mlvOut = fopen(pathName.toUtf8().data(), "wb");
#else
    FILE* mlvOut = fopen(pathName.toLatin1().data(), "wb");
#endif
    if (!mlvOut)
    {
        return;
    }

    //Allocate buffer for averaging
    uint64_t * averagedImage = NULL;
    if( m_codecOption == CODEC_MLV_AVERAGED ) averagedImage = (uint64_t *)calloc( m_pMlvObject->RAWI.xRes * m_pMlvObject->RAWI.yRes * sizeof( uint64_t ), 1 );
    //Check if MLV has audio and it is requested to be exported
    int exportAudio = (doesMlvHaveAudio( m_pMlvObject ) && m_audioExportEnabled);
    //Error message string passed from backend
    char errorMessage[256] = { 0 };
    //Save MLV block headers
    int ret = saveMlvHeaders( m_pMlvObject, mlvOut, exportAudio, m_codecOption, m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut(), VERSION, errorMessage );
    //Output frames loop
    for( uint32_t frame = m_exportQueue.first()->cutIn() - 1; frame < m_exportQueue.first()->cutOut(); frame++ )
    {
        //Save audio and video frames
        if( ret || saveMlvAVFrame( m_pMlvObject, mlvOut, exportAudio, m_codecOption, m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut(), frame , averagedImage, errorMessage) )
        {
            fclose(mlvOut); mlvOut = NULL;
            QFile( pathName ).remove();

            ret = QMessageBox::critical( this,
                                         tr( "MLV App - Export file error" ),
                                         tr( "%1" ).arg( errorMessage ),
                                         tr( "Abort current export" ),
                                         tr( "Abort batch export" ),
                                         0, 1 );
            if( ret ) exportAbort();
            else break;
        }
        else
        {
            //Set Status
            m_pStatusDialog->ui->progressBar->setValue( frame - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
            m_pStatusDialog->ui->progressBar->repaint();
            m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - frame + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
            qApp->processEvents();
        }
        //Abort pressed? -> End the loop
        if( m_exportAbortPressed || m_codecOption == CODEC_MLV_EXTRACT_DF) break;
    }
    //Clean up
    if( averagedImage ) free( averagedImage );
    if( mlvOut ) fclose(mlvOut);
    //Enable GUI drawing
    m_dontDraw = false;
    //Emit Ready-Signal
    emit exportReady();
}

//Export via AVFoundation
#ifdef Q_OS_MACX
void MainWindow::startExportAVFoundation(QString fileName)
{
    //Disable GUI drawing
    m_dontDraw = true;

    //chose if we want to get amaze frames for exporting, or bilinear
    if( m_exportDebayerMode == 0 )
    {
        setMlvDontAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 1 )
    {
        setMlvAlwaysUseAmaze( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 2 )
    {
        setMlvUseLmmseDebayer( m_pMlvObject );
    }
    else if( m_exportDebayerMode == 3 )
    {
        setMlvUseIgvDebayer( m_pMlvObject );
    }
    else
    {
        switch( m_exportQueue.first()->debayer() )
        {
        case ReceiptSettings::None:
            setMlvUseNoneDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Simple:
            setMlvUseSimpleDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Bilinear:
            setMlvDontAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::LMMSE:
            setMlvUseLmmseDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::IGV:
            setMlvUseIgvDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::AMaZE:
            setMlvAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::AHD:
            setMlvUseAhdDebayer( m_pMlvObject );
            break;
        default:
            break;
        }
    }
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_pMlvObject->current_cached_frame_active = 0;
    //enable low level raw fixes (if wanted)
    if( ui->checkBoxRawFixEnable->isChecked() ) m_pMlvObject->llrawproc->fix_raw = 1;

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( m_exportQueue.first()->cutOut() - m_exportQueue.first()->cutIn() + 1 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->open();
    //Frames in the export queue?!
    int totalFrames = 0;
    for( int i = 0; i < m_exportQueue.count(); i++ )
    {
        totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
    }

    //If file exists, delete it!
    QFile *file = new QFile( fileName );
    if( file->exists() ) file->remove();
    delete file;

    //Codec?
    int avfCodec;
    if( m_codecProfile == CODEC_PRORES422ST ) avfCodec = AVF_CODEC_PRORES_422;
    else if( m_codecProfile == CODEC_H264 ) avfCodec = AVF_CODEC_H264;
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
    else if( m_codecProfile == CODEC_H265 ) avfCodec = AVF_CODEC_HEVC;
#endif
    else avfCodec = AVF_CODEC_PRORES_4444;

    //Dimension & scaling
    uint16_t width = getMlvWidth(m_pMlvObject);
    uint16_t height = getMlvHeight(m_pMlvObject);
    bool scaled = false;
    if( m_resizeFilterEnabled )
    {
        //Autocalc height
        if( m_resizeFilterHeightLocked )
        {
            height = (double)m_resizeWidth / (double)getMlvWidth( m_pMlvObject )
                    / m_exportQueue.first()->stretchFactorX()
                    * m_exportQueue.first()->stretchFactorY()
                    * (double)getMlvHeight( m_pMlvObject ) + 0.5;
        }
        else
        {
            height = m_resizeHeight;
        }
        width = m_resizeWidth;
        scaled = true;
    }
    else if( m_exportQueue.first()->stretchFactorX() != 1.0
          || m_exportQueue.first()->stretchFactorY() != 1.0 )
    {
        //Upscale only
        if( m_exportQueue.first()->stretchFactorY() == STRETCH_V_033 )
        {
            width = getMlvWidth( m_pMlvObject ) * 3;
            height = getMlvHeight( m_pMlvObject );
        }
        else
        {
            width = getMlvWidth( m_pMlvObject ) * m_exportQueue.first()->stretchFactorX();
            height = getMlvHeight( m_pMlvObject ) * m_exportQueue.first()->stretchFactorY();
        }
        scaled = true;
    }
    if( m_codecProfile == CODEC_H264
     || m_codecProfile == CODEC_H265 )
    {
        if( width != width + (width % 2) )
        {
            width += width % 2;
            scaled = true;
        }
        if( height != height + (height % 2) )
        {
            height += height % 2;
            scaled = true;
        }
    }

    //Init Encoder
    AVEncoder_t * encoder = initAVEncoder( width,
                                           height,
                                           avfCodec,
                                           AVF_COLOURSPACE_SRGB,
                                           getFramerate() );

    beginWritingVideoFile(encoder, fileName.toUtf8().data());

    //Build buffer
    uint32_t frameSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
    uint16_t * imgBuffer;
    imgBuffer = ( uint16_t* )malloc( frameSize * sizeof( uint16_t ) );
    uint16_t * imgBufferScaled;
    uint8_t * imgBufferScaled8;
    if( m_codecProfile != CODEC_H264 ) imgBufferScaled = ( uint16_t* )malloc( width * height * 3 * sizeof( uint16_t ) );
    else imgBufferScaled8 = ( uint8_t* )malloc( width * height * 3 * sizeof( uint8_t ) );

    //Encoder frames
    for( uint64_t frame = ( m_exportQueue.first()->cutIn() - 1 ); frame < m_exportQueue.first()->cutOut(); frame++ )
    {
        //Get&Encode
        if( m_codecProfile == CODEC_H264 )
        {
            getMlvProcessedFrame8( m_pMlvObject, frame, m_pRawImage, QThread::idealThreadCount() );
            if( scaled )
            {
                avir_scale_thread_pool scaling_pool;
                avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                avir::CImageResizerParamsUltra roptions;
                avir::CImageResizer<> image_resizer( 8, 0, roptions );
                image_resizer.resizeImage( m_pRawImage,
                                           getMlvWidth(m_pMlvObject),
                                           getMlvHeight(m_pMlvObject), 0,
                                           imgBufferScaled8,
                                           width,
                                           height,
                                           3, 0, &vars );
                addFrameToVideoFile8bit( encoder, imgBufferScaled8 );
            }
            else
            {
                addFrameToVideoFile8bit( encoder, m_pRawImage );
            }
        }
        else
        {
            getMlvProcessedFrame16( m_pMlvObject, frame, imgBuffer, QThread::idealThreadCount() );
            if( scaled )
            {
                avir_scale_thread_pool scaling_pool;
                avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                avir::CImageResizerParamsUltra roptions;
                avir::CImageResizer<> image_resizer( 16, 0, roptions );
                image_resizer.resizeImage( imgBuffer,
                                           getMlvWidth(m_pMlvObject),
                                           getMlvHeight(m_pMlvObject), 0,
                                           imgBufferScaled,
                                           width,
                                           height,
                                           3, 0, &vars );
                addFrameToVideoFile( encoder, imgBufferScaled );
            }
            else
            {
                addFrameToVideoFile( encoder, imgBuffer );
            }
        }

        //Set Status
        m_pStatusDialog->ui->progressBar->setValue( frame - ( m_exportQueue.first()->cutIn() - 1 ) + 1 );
        m_pStatusDialog->ui->progressBar->repaint();
        m_pStatusDialog->drawTimeFromToDoFrames( totalFrames - frame + ( m_exportQueue.first()->cutIn() - 1 ) - 1 );
        qApp->processEvents();

        //Abort pressed? -> End the loop
        if( m_exportAbortPressed ) break;
    }

    //Clean up
    if( m_codecProfile != CODEC_H264 ) free( imgBufferScaled );
    else free( imgBufferScaled8 );
    free( imgBuffer );
    endWritingVideoFile(encoder);
    freeAVEncoder(encoder);

    //Audio
    if( m_audioExportEnabled && doesMlvHaveAudio( m_pMlvObject ) && !m_exportAbortPressed )
    {
        QString wavFileName = QString( "%1.wav" ).arg( fileName.left( fileName.lastIndexOf( "." ) ) );
        writeMlvAudioToWaveCut( m_pMlvObject, wavFileName.toUtf8().data(), m_exportQueue.first()->cutIn(), m_exportQueue.first()->cutOut() );

        QString tempFileName = QString( "%1_temp.mov" ).arg( fileName.left( fileName.lastIndexOf( "." ) ) );
        QFile( fileName ).rename( tempFileName );

        //FFMpeg export
        QString ffmpegAudioCommand = QCoreApplication::applicationDirPath();
        ffmpegAudioCommand.append( QString( "/ffmpeg\"" ) );
        ffmpegAudioCommand.prepend( QString( "\"" ) );

#ifdef STDOUT_SILENT
        ffmpegAudioCommand.append( QString( " -loglevel 0" ) );
#endif

        ffmpegAudioCommand.append( QString( " -y -i \"%1\" -i \"%2\" -map 0:0 -map 1:0 -c copy \"%3\"" )
                .arg( tempFileName ).arg( wavFileName ).arg( fileName ) );

        QProcess ffmpegProc;
        //qDebug() << ffmpegAudioCommand <<
        while( !QFileInfo( fileName ).exists()  ) //AVFoundation file will be corrupted for a unknown time until we can add audio
        {
            ffmpegProc.execute( ffmpegAudioCommand );

            qApp->processEvents();

            //Abort pressed? -> End the loop
            if( m_exportAbortPressed ) break;
        }

        QFile( tempFileName ).remove();
        QFile( wavFileName ).remove();
    }

    //If we don't like amaze we switch it off again
    if( !ui->actionAlwaysUseAMaZE->isChecked() ) setMlvDontAlwaysUseAmaze( m_pMlvObject );

    //Enable GUI drawing
    m_dontDraw = false;

    //Emit Ready-Signal
    emit exportReady();
}
#endif

//Adds the fileName to the Session List
void MainWindow::addFileToSession(QString fileName)
{
    //Save settings of actual clip (if there is one)
    if( m_pSessionReceipts.count() > 0 )
    {
        if( !m_pSessionReceipts.at( m_lastActiveClipInSession )->wasNeverLoaded() )
        {
            setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );
        }
    }
    //Add to session list (empty Pixmap is just spacer)
    QListWidgetItem *item = new QListWidgetItem( QFileInfo(fileName).fileName() );
    item->setToolTip( fileName );
    ui->listWidgetSession->addItem( item );
    //Set sliders
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    sliders->setFileName( fileName );
    m_pSessionReceipts.append( sliders );
    //Save index of active clip
    m_lastActiveClipInSession = ui->listWidgetSession->row( item );
    //Set this row to current row
    ui->listWidgetSession->clearSelection();
    ui->listWidgetSession->setCurrentItem( item );
    //Update App
    qApp->processEvents();
}

//Open a session file
void MainWindow::openSession(QString fileNameSession)
{
    bool abort = false;
    bool skipAll = false;
    QXmlStreamReader Rxml;
    QFile file(fileNameSession);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

    //Version of settings (values may be interpreted differently)
    int versionMasxml = 0;

    //Clear the last session
    deleteSession();

    //Parse
    Rxml.setDevice(&file);
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        //qDebug() << "InWhile";
        if( Rxml.isStartElement() && Rxml.name() == "mlv_files" )
        {
            //Read version string, if there is one
            if( Rxml.attributes().count() != 0 )
            {
                //qDebug() << "masxmlVersion" << Rxml.attributes().at(0).value().toInt();
                versionMasxml = Rxml.attributes().at(0).value().toInt();
            }
            //qDebug() << "StartElem";
            while( !Rxml.atEnd() && !Rxml.isEndElement() && !abort )
            {
                Rxml.readNext();
                if( Rxml.isStartElement() && Rxml.name() == "clip" )
                {
                    //qDebug() << "Clip!" << Rxml.attributes().at(0).name() << Rxml.attributes().at(0).value();
                    QString fileName = Rxml.attributes().at(0).value().toString();
                    //If file is not there, search at alternative relative path for file
                    if( !QFile( fileName ).exists() )
                    {
                        if( Rxml.attributes().count() > 1 )
                        {
                            QString relativeName = Rxml.attributes().at(1).value().toString();
                            fileName = QDir( QFileInfo( fileNameSession ).path() ).filePath( relativeName );
                        }
                    }

                    if( QFile( fileName ).exists() )
                    {
                        //Save last file name
                        m_lastSessionFileName = fileName;
                        //Add file to Sessionlist
                        addFileToSession( fileName );
                        //Open the file
                        openMlvForPreview( fileName );
                        m_pSessionReceipts.last()->setFileName( fileName );

                        readXmlElementsFromFile( &Rxml, m_pSessionReceipts.last(), versionMasxml );
                        setSliders( m_pSessionReceipts.last(), false );
                        previewPicture( ui->listWidgetSession->count() - 1 );
                        m_lastActiveClipInSession = ui->listWidgetSession->count() - 1;
                    }
                    else
                    {
                        if( !skipAll )
                        {
                            int ret = QMessageBox::critical( this, tr( "Open Session Error" ), tr( "File not found: \r\n%1" ).arg( fileName ), tr( "Skip" ), tr( "Skip all"), tr( "Abort" ) );
                            if( ret == 1 )
                            {
                                skipAll = true;
                            }
                            else if( ret == 2 )
                            {
                                abort = true;
                            }
                        }

                        //If file does not exist we just parse uninteresting data in the right way
                        while( !Rxml.atEnd() && !Rxml.isEndElement() )
                        {
                            Rxml.readNext();
                            if( Rxml.isStartElement() ) //future features
                            {
                                Rxml.readElementText();
                                Rxml.readNext();
                            }
                        }
                    }
                    Rxml.readNext();
                }
                else if( Rxml.isEndElement() )
                {
                    //qDebug() << "EndElement! (clip)";
                    Rxml.readNext();
                }
            }
        }
    }

    file.close();

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    if (Rxml.hasError())
    {
        QMessageBox::critical( this, tr( "Open Session" ), tr( "Error: Failed to parse file! %1" )
                               .arg( Rxml.errorString() ) );
        return;
    }
    else if (file.error() != QFile::NoError)
    {
        QMessageBox::critical( this, tr( "Open Session" ), tr( "Error: Cannot read file! %1" ).arg( file.errorString() ) );
        return;
    }

    m_pRecentFilesMenu->addRecentFile( QDir::toNativeSeparators( fileNameSession ) );
}

//Save Session
void MainWindow::saveSession(QString fileName)
{
    //Save slider receipt
    setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );

    QFile file(fileName);
    file.open(QIODevice::WriteOnly);

    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();

    xmlWriter.writeStartElement( "mlv_files" );
    xmlWriter.writeAttribute( "version", "3" );
    xmlWriter.writeAttribute( "mlvapp", VERSION );
    for( int i = 0; i < ui->listWidgetSession->count(); i++ )
    {
        xmlWriter.writeStartElement( "clip" );
        xmlWriter.writeAttribute( "file", ui->listWidgetSession->item(i)->toolTip() );
        xmlWriter.writeAttribute( "relative", QDir( QFileInfo( fileName ).path() ).relativeFilePath( ui->listWidgetSession->item(i)->toolTip() ) );
        writeXmlElementsToFile( &xmlWriter, m_pSessionReceipts.at(i) );
        xmlWriter.writeEndElement();
    }
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();

    file.close();

    m_pRecentFilesMenu->addRecentFile( QDir::toNativeSeparators( fileName ) );
}


//Imports and sets slider settings from a file to the sliders
void MainWindow::on_actionImportReceipt_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //If no clip loaded, abort
    if( m_pSessionReceipts.empty() ) return;

    QString path = QFileInfo( m_lastReceiptFileName ).absolutePath();
    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open MLV App Receipt Xml"), path,
                                           tr("MLV App Receipt Xml files (*.marxml)"));

    //Abort selected
    if( fileName.count() == 0 ) return;
    m_lastReceiptFileName = fileName;

    //Open a XML stream for the file
    QXmlStreamReader Rxml;
    QFile file(fileName);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

    //Version of settings (values may be interpreted differently)
    int versionReceipt = 0;

    //Parse
    Rxml.setDevice(&file);
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        if( Rxml.isStartElement() && Rxml.name() == "receipt" )
        {
            //Read version string, if there is one
            if( Rxml.attributes().count() != 0 )
            {
                //qDebug() << "masxmlVersion" << Rxml.attributes().at(0).value().toInt();
                versionReceipt = Rxml.attributes().at(0).value().toInt();
            }
            readXmlElementsFromFile( &Rxml, m_pReceiptClipboard, versionReceipt );
        }
    }
    file.close();

    m_pCopyMask->exec();
    ui->actionPasteReceipt->setEnabled( true );
    on_actionPasteReceipt_triggered();
}

//Exports the actual slider settings to a file
void MainWindow::on_actionExportReceipt_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastReceiptFileName ).absolutePath();
    QString fileName = QFileDialog::getSaveFileName(this,
                                           tr("Save MLV App Receipt Xml"), path,
                                           tr("MLV App Receipt Xml files (*.marxml)"));

    //Abort selected
    if( fileName.count() == 0 ) return;
    m_lastReceiptFileName = fileName;

    //Save slider receipt
    setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );

    QFile file(fileName);
    file.open(QIODevice::WriteOnly);

    //Open a XML writer
    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();

    xmlWriter.writeStartElement( "receipt" );
    xmlWriter.writeAttribute( "version", "3" );
    xmlWriter.writeAttribute( "mlvapp", VERSION );

    writeXmlElementsToFile( &xmlWriter, m_pSessionReceipts.at( m_lastActiveClipInSession ) );

    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();

    file.close();
}

//Read all receipt elements from xml
void MainWindow::readXmlElementsFromFile(QXmlStreamReader *Rxml, ReceiptSettings *receipt, int version)
{
    //Compatibility for Cam Matrix (files without the tag will disable it
    receipt->setCamMatrixUsed( false );

    //Compatibility for old saved dual iso projects
    receipt->setDualIsoForced( DISO_FORCED );

    //Read tags
    while( !Rxml->atEnd() && !Rxml->isEndElement() )
    {
        Rxml->readNext();
        if( Rxml->isStartElement() && Rxml->name() == "exposure" )
        {
            receipt->setExposure( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "contrast" )
        {
            receipt->setContrast( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "temperature" )
        {
            receipt->setTemperature( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "tint" )
        {
            receipt->setTint( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "clarity" )
        {
            receipt->setClarity( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vibrance" )
        {
            receipt->setVibrance( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "saturation" )
        {
            if( version < 2 ) receipt->setSaturation( ( Rxml->readElementText().toInt() * 2.0 ) - 100.0 );
            else receipt->setSaturation( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "ls" )
        {
            if( version < 2 ) receipt->setLs( Rxml->readElementText().toInt() * 10.0 / FACTOR_LS );
            else receipt->setLs( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "lr" )
        {
            receipt->setLr( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "ds" )
        {
            if( version < 2 ) receipt->setDs( Rxml->readElementText().toInt() * 10.0 / FACTOR_DS );
            else receipt->setDs( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dr" )
        {
            receipt->setDr( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "lightening" )
        {
            if( version < 2 ) receipt->setLightening( Rxml->readElementText().toInt() / FACTOR_LIGHTEN );
            else receipt->setLightening( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "shadows" )
        {
            receipt->setShadows( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "highlights" )
        {
            receipt->setHighlights( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradationCurve" )
        {
            receipt->setGradationCurve( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "hueVsHue" )
        {
            receipt->setHueVsHue( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "hueVsSaturation" )
        {
            receipt->setHueVsSaturation( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "hueVsLuminance" )
        {
            receipt->setHueVsLuminance( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "lumaVsSaturation" )
        {
            receipt->setLumaVsSaturation( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientEnabled" )
        {
            receipt->setGradientEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientExposure" )
        {
            receipt->setGradientExposure( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientContrast" )
        {
            receipt->setGradientContrast( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientStartX" )
        {
            receipt->setGradientStartX( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientStartY" )
        {
            receipt->setGradientStartY( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientLength" )
        {
            receipt->setGradientLength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gradientAngle" )
        {
            receipt->setGradientAngle( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "sharpen" )
        {
            receipt->setSharpen( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "sharpenMasking" )
        {
            receipt->setShMasking( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "chromaBlur" )
        {
            receipt->setChromaBlur( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "highlightReconstruction" )
        {
            receipt->setHighlightReconstruction( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "camMatrixUsed" )
        {
            receipt->setCamMatrixUsed( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "chromaSeparation" )
        {
            receipt->setChromaSeparation( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "profile" )
        {
            uint8_t profile = (uint8_t)Rxml->readElementText().toUInt();
            if( version < 2 && profile > 1 ) receipt->setProfile( profile + 2 );
            else if( version == 2 )
            {
                receipt->setProfile( profile + 1 );
                receipt->setGamut( GAMUT_Rec709 );
                if( ( profile != PROFILE_ALEXA_LOG )
                 && ( profile != PROFILE_CINEON_LOG )
                 && ( profile != PROFILE_SONY_LOG_3 )
                 && ( profile != PROFILE_SRGB )
                 && ( profile != PROFILE_REC709 )
                 && ( profile != PROFILE_BMDFILM ) )
                {
                    receipt->setAllowCreativeAdjustments( true );
                }
                else
                {
                    receipt->setAllowCreativeAdjustments( false );
                }
                switch( profile )
                {
                case PROFILE_STANDARD:
                case PROFILE_TONEMAPPED:
                    receipt->setGamma( 315 );
                    break;
                case PROFILE_FILM:
                    receipt->setGamma( 346 );
                    break;
                default:
                    receipt->setGamma( 100 );
                    break;
                }
            }
            //else receipt->setProfile( profile ); //never load for v3, because we now have single settings
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "tonemap" )
        {
            receipt->setTonemap( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gamut" )
        {
            receipt->setGamut( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "gamma" )
        {
            receipt->setGamma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "allowCreativeAdjustments" )
        {
            receipt->setAllowCreativeAdjustments( (bool)Rxml->readElementText().toInt() );
            if( version == 2 )
            {
                int profile = receipt->profile();
                if( ( profile != PROFILE_ALEXA_LOG )
                 && ( profile != PROFILE_CINEON_LOG )
                 && ( profile != PROFILE_SONY_LOG_3 )
                 && ( profile != PROFILE_SRGB )
                 && ( profile != PROFILE_REC709 )
                 && ( profile != PROFILE_BMDFILM ) )
                {
                    receipt->setAllowCreativeAdjustments( true );
                }
            }
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "denoiserWindow" )
        {
            receipt->setDenoiserWindow( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "denoiserStrength" )
        {
            receipt->setDenoiserStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "rbfDenoiserLuma" )
        {
            receipt->setRbfDenoiserLuma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "rbfDenoiserChroma" )
        {
            receipt->setRbfDenoiserChroma( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "rbfDenoiserRange" )
        {
            receipt->setRbfDenoiserRange( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "grainStrength" )
        {
            receipt->setGrainStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "rawFixesEnabled" )
        {
            receipt->setRawFixesEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "verticalStripes" )
        {
            receipt->setVerticalStripes( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "focusPixels" )
        {
            receipt->setFocusPixels( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "fpiMethod" )
        {
            receipt->setFpiMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "badPixels" )
        {
            receipt->setBadPixels( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "bpsMethod" )
        {
            receipt->setBpsMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "bpiMethod" )
        {
            receipt->setBpiMethod( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "chromaSmooth" )
        {
            receipt->setChromaSmooth( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "patternNoise" )
        {
            receipt->setPatternNoise( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "deflickerTarget" )
        {
            receipt->setDeflickerTarget( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIsoForced" )
        {
            receipt->setDualIsoForced( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIso" )
        {
            receipt->setDualIso( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIsoInterpolation" )
        {
            receipt->setDualIsoInterpolation( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIsoAliasMap" )
        {
            receipt->setDualIsoAliasMap( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIsoFrBlending" )
        {
            receipt->setDualIsoFrBlending( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIsoWhite" )
        {
            receipt->setDualIsoWhite( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "dualIsoBlack" )
        {
            receipt->setDualIsoBlack( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "darkFrameFileName" )
        {
            receipt->setDarkFrameFileName( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "darkFrameEnabled" )
        {
            receipt->setDarkFrameEnabled( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "rawBlack" )
        {
            receipt->setRawBlack( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "rawWhite" )
        {
            receipt->setRawWhite( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "tone" )
        {
            receipt->setTone( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "toningStrength" )
        {
            receipt->setToningStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "lutEnabled" )
        {
            receipt->setLutEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "lutName" )
        {
            receipt->setLutName( Rxml->readElementText() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "lutStrength" )
        {
            receipt->setLutStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "filterEnabled" )
        {
            receipt->setFilterEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "filterIndex" )
        {
            receipt->setFilterIndex( Rxml->readElementText().toUInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "filterStrength" )
        {
            receipt->setFilterStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vignetteStrength" )
        {
            receipt->setVignetteStrength( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vignetteRadius" )
        {
            receipt->setVignetteRadius( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vignetteShape" )
        {
            receipt->setVignetteShape( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "caRed" )
        {
            receipt->setCaRed( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "caBlue" )
        {
            receipt->setCaBlue( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "caDesaturate" )
        {
            receipt->setCaDesaturate( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "caRadius" )
        {
            receipt->setCaRadius( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "stretchFactorX" )
        {
            receipt->setStretchFactorX( Rxml->readElementText().toDouble() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "stretchFactorY" )
        {
            receipt->setStretchFactorY( Rxml->readElementText().toDouble() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "upsideDown" )
        {
            receipt->setUpsideDown( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabEnable" )
        {
            receipt->setVidstabEnabled( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabStepsize" )
        {
            receipt->setVidstabStepsize( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabShakiness" )
        {
            receipt->setVidstabShakiness( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabAccuracy" )
        {
            receipt->setVidstabAccuracy( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabZoom" )
        {
            receipt->setVidstabZoom( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabSmoothing" )
        {
            receipt->setVidstabSmoothing( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "vidstabTripod" )
        {
            receipt->setVidstabTripod( (bool)Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "cutIn" )
        {
            receipt->setCutIn( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "cutOut" )
        {
            receipt->setCutOut( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() && Rxml->name() == "debayer" )
        {
            receipt->setDebayer( Rxml->readElementText().toInt() );
            Rxml->readNext();
        }
        else if( Rxml->isStartElement() ) //future features
        {
            Rxml->readElementText();
            Rxml->readNext();
        }
    }
}

//Write all receipt elements to xml
void MainWindow::writeXmlElementsToFile(QXmlStreamWriter *xmlWriter, ReceiptSettings *receipt)
{
    xmlWriter->writeTextElement( "exposure",                QString( "%1" ).arg( receipt->exposure() ) );
    xmlWriter->writeTextElement( "contrast",                QString( "%1" ).arg( receipt->contrast() ) );
    xmlWriter->writeTextElement( "temperature",             QString( "%1" ).arg( receipt->temperature() ) );
    xmlWriter->writeTextElement( "tint",                    QString( "%1" ).arg( receipt->tint() ) );
    xmlWriter->writeTextElement( "clarity",                 QString( "%1" ).arg( receipt->clarity() ) );
    xmlWriter->writeTextElement( "vibrance",                QString( "%1" ).arg( receipt->vibrance() ) );
    xmlWriter->writeTextElement( "saturation",              QString( "%1" ).arg( receipt->saturation() ) );
    xmlWriter->writeTextElement( "ds",                      QString( "%1" ).arg( receipt->ds() ) );
    xmlWriter->writeTextElement( "dr",                      QString( "%1" ).arg( receipt->dr() ) );
    xmlWriter->writeTextElement( "ls",                      QString( "%1" ).arg( receipt->ls() ) );
    xmlWriter->writeTextElement( "lr",                      QString( "%1" ).arg( receipt->lr() ) );
    xmlWriter->writeTextElement( "lightening",              QString( "%1" ).arg( receipt->lightening() ) );
    xmlWriter->writeTextElement( "gradationCurve",          QString( "%1" ).arg( receipt->gradationCurve() ) );
    xmlWriter->writeTextElement( "hueVsHue",                QString( "%1" ).arg( receipt->hueVsHue() ) );
    xmlWriter->writeTextElement( "hueVsSaturation",         QString( "%1" ).arg( receipt->hueVsSaturation() ) );
    xmlWriter->writeTextElement( "hueVsLuminance",          QString( "%1" ).arg( receipt->hueVsLuminance() ) );
    xmlWriter->writeTextElement( "lumaVsSaturation",        QString( "%1" ).arg( receipt->lumaVsSaturation() ) );
    xmlWriter->writeTextElement( "shadows",                 QString( "%1" ).arg( receipt->shadows() ) );
    xmlWriter->writeTextElement( "highlights",              QString( "%1" ).arg( receipt->highlights() ) );
    xmlWriter->writeTextElement( "gradientEnabled",         QString( "%1" ).arg( receipt->isGradientEnabled() ) );
    xmlWriter->writeTextElement( "gradientExposure",        QString( "%1" ).arg( receipt->gradientExposure() ) );
    xmlWriter->writeTextElement( "gradientContrast",        QString( "%1" ).arg( receipt->gradientContrast() ) );
    xmlWriter->writeTextElement( "gradientStartX",          QString( "%1" ).arg( receipt->gradientStartX() ) );
    xmlWriter->writeTextElement( "gradientStartY",          QString( "%1" ).arg( receipt->gradientStartY() ) );
    xmlWriter->writeTextElement( "gradientLength",          QString( "%1" ).arg( receipt->gradientLength() ) );
    xmlWriter->writeTextElement( "gradientAngle",           QString( "%1" ).arg( receipt->gradientAngle() ) );
    xmlWriter->writeTextElement( "sharpen",                 QString( "%1" ).arg( receipt->sharpen() ) );
    xmlWriter->writeTextElement( "sharpenMasking",          QString( "%1" ).arg( receipt->shMasking() ) );
    xmlWriter->writeTextElement( "chromaBlur",              QString( "%1" ).arg( receipt->chromaBlur() ) );
    xmlWriter->writeTextElement( "highlightReconstruction", QString( "%1" ).arg( receipt->isHighlightReconstruction() ) );
    xmlWriter->writeTextElement( "camMatrixUsed",           QString( "%1" ).arg( receipt->camMatrixUsed() ) );
    xmlWriter->writeTextElement( "chromaSeparation",        QString( "%1" ).arg( receipt->isChromaSeparation() ) );
    //xmlWriter->writeTextElement( "profile",                 QString( "%1" ).arg( receipt->profile() ) );
    xmlWriter->writeTextElement( "tonemap",                 QString( "%1" ).arg( receipt->tonemap() ) );
    xmlWriter->writeTextElement( "gamut",                   QString( "%1" ).arg( receipt->gamut() ) );
    xmlWriter->writeTextElement( "gamma",                   QString( "%1" ).arg( receipt->gamma() ) );
    xmlWriter->writeTextElement( "allowCreativeAdjustments",QString( "%1" ).arg( receipt->allowCreativeAdjustments() ) );
    xmlWriter->writeTextElement( "denoiserStrength",        QString( "%1" ).arg( receipt->denoiserStrength() ) );
    xmlWriter->writeTextElement( "denoiserWindow",          QString( "%1" ).arg( receipt->denoiserWindow() ) );
    xmlWriter->writeTextElement( "rbfDenoiserLuma",         QString( "%1" ).arg( receipt->rbfDenoiserLuma() ) );
    xmlWriter->writeTextElement( "rbfDenoiserChroma",       QString( "%1" ).arg( receipt->rbfDenoiserChroma() ) );
    xmlWriter->writeTextElement( "rbfDenoiserRange",        QString( "%1" ).arg( receipt->rbfDenoiserRange() ) );
    xmlWriter->writeTextElement( "grainStrength",           QString( "%1" ).arg( receipt->grainStrength() ) );
    xmlWriter->writeTextElement( "rawFixesEnabled",         QString( "%1" ).arg( receipt->rawFixesEnabled() ) );
    xmlWriter->writeTextElement( "verticalStripes",         QString( "%1" ).arg( receipt->verticalStripes() ) );
    xmlWriter->writeTextElement( "focusPixels",             QString( "%1" ).arg( receipt->focusPixels() ) );
    xmlWriter->writeTextElement( "fpiMethod",               QString( "%1" ).arg( receipt->fpiMethod() ) );
    xmlWriter->writeTextElement( "badPixels",               QString( "%1" ).arg( receipt->badPixels() ) );
    xmlWriter->writeTextElement( "bpsMethod",               QString( "%1" ).arg( receipt->bpsMethod() ) );
    xmlWriter->writeTextElement( "bpiMethod",               QString( "%1" ).arg( receipt->bpiMethod() ) );
    xmlWriter->writeTextElement( "chromaSmooth",            QString( "%1" ).arg( receipt->chromaSmooth() ) );
    xmlWriter->writeTextElement( "patternNoise",            QString( "%1" ).arg( receipt->patternNoise() ) );
    xmlWriter->writeTextElement( "deflickerTarget",         QString( "%1" ).arg( receipt->deflickerTarget() ) );
    xmlWriter->writeTextElement( "dualIsoForced",           QString( "%1" ).arg( receipt->dualIsoForced() ) );
    xmlWriter->writeTextElement( "dualIso",                 QString( "%1" ).arg( receipt->dualIso() ) );
    xmlWriter->writeTextElement( "dualIsoInterpolation",    QString( "%1" ).arg( receipt->dualIsoInterpolation() ) );
    xmlWriter->writeTextElement( "dualIsoAliasMap",         QString( "%1" ).arg( receipt->dualIsoAliasMap() ) );
    xmlWriter->writeTextElement( "dualIsoFrBlending",       QString( "%1" ).arg( receipt->dualIsoFrBlending() ) );
    xmlWriter->writeTextElement( "dualIsoWhite",            QString( "%1" ).arg( receipt->dualIsoWhite() ) );
    xmlWriter->writeTextElement( "dualIsoBlack",            QString( "%1" ).arg( receipt->dualIsoBlack() ) );
    xmlWriter->writeTextElement( "darkFrameFileName",       QString( "%1" ).arg( receipt->darkFrameFileName() ) );
    xmlWriter->writeTextElement( "darkFrameEnabled",        QString( "%1" ).arg( receipt->darkFrameEnabled() ) );
    xmlWriter->writeTextElement( "rawBlack",                QString( "%1" ).arg( receipt->rawBlack() ) );
    xmlWriter->writeTextElement( "rawWhite",                QString( "%1" ).arg( receipt->rawWhite() ) );
    xmlWriter->writeTextElement( "tone",                    QString( "%1" ).arg( receipt->tone() ) );
    xmlWriter->writeTextElement( "toningStrength",          QString( "%1" ).arg( receipt->toningStrength() ) );
    xmlWriter->writeTextElement( "lutEnabled",              QString( "%1" ).arg( receipt->lutEnabled() ) );
    xmlWriter->writeTextElement( "lutName",                 QString( "%1" ).arg( receipt->lutName() ) );
    xmlWriter->writeTextElement( "lutStrength",             QString( "%1" ).arg( receipt->lutStrength() ) );
    xmlWriter->writeTextElement( "filterEnabled",           QString( "%1" ).arg( receipt->filterEnabled() ) );
    xmlWriter->writeTextElement( "filterIndex",             QString( "%1" ).arg( receipt->filterIndex() ) );
    xmlWriter->writeTextElement( "filterStrength",          QString( "%1" ).arg( receipt->filterStrength() ) );
    xmlWriter->writeTextElement( "vignetteStrength",        QString( "%1" ).arg( receipt->vignetteStrength() ) );
    xmlWriter->writeTextElement( "vignetteRadius",          QString( "%1" ).arg( receipt->vignetteRadius() ) );
    xmlWriter->writeTextElement( "vignetteShape",           QString( "%1" ).arg( receipt->vignetteShape() ) );
    xmlWriter->writeTextElement( "caRed",                   QString( "%1" ).arg( receipt->caRed() ) );
    xmlWriter->writeTextElement( "caBlue",                  QString( "%1" ).arg( receipt->caBlue() ) );
    xmlWriter->writeTextElement( "caDesaturate",            QString( "%1" ).arg( receipt->caDesaturate() ) );
    xmlWriter->writeTextElement( "caRadius",                QString( "%1" ).arg( receipt->caRadius() ) );
    xmlWriter->writeTextElement( "stretchFactorX",          QString( "%1" ).arg( receipt->stretchFactorX() ) );
    xmlWriter->writeTextElement( "stretchFactorY",          QString( "%1" ).arg( receipt->stretchFactorY() ) );
    xmlWriter->writeTextElement( "upsideDown",              QString( "%1" ).arg( receipt->upsideDown() ) );
    xmlWriter->writeTextElement( "vidstabEnable",           QString( "%1" ).arg( receipt->vidStabEnabled() ) );
    xmlWriter->writeTextElement( "vidstabStepsize",         QString( "%1" ).arg( receipt->vidStabStepsize() ) );
    xmlWriter->writeTextElement( "vidstabShakiness",        QString( "%1" ).arg( receipt->vidStabShakiness() ) );
    xmlWriter->writeTextElement( "vidstabAccuracy",         QString( "%1" ).arg( receipt->vidStabAccuracy() ) );
    xmlWriter->writeTextElement( "vidstabZoom",             QString( "%1" ).arg( receipt->vidStabZoom() ) );
    xmlWriter->writeTextElement( "vidstabSmoothing",        QString( "%1" ).arg( receipt->vidStabSmoothing() ) );
    xmlWriter->writeTextElement( "vidstabTripod",           QString( "%1" ).arg( receipt->vidStabTripod() ) );
    xmlWriter->writeTextElement( "cutIn",                   QString( "%1" ).arg( receipt->cutIn() ) );
    xmlWriter->writeTextElement( "cutOut",                  QString( "%1" ).arg( receipt->cutOut() ) );
    xmlWriter->writeTextElement( "debayer",                 QString( "%1" ).arg( receipt->debayer() ) );
}

//Delete all clips from Session
void MainWindow::deleteSession()
{
    //Clear the memory
    m_pSessionReceipts.clear();
    ui->listWidgetSession->clear();

    //Set window title to filename
    this->setWindowTitle( QString( "MLV App" ) );

    //disable drawing and kill old timer and old WaveFormMonitor
    m_fileLoaded = false;
    m_dontDraw = true;

    //Set Labels black
    ui->labelScope->setScope( NULL, 0, 0, false, false, ScopesLabel::None );
    m_pGraphicsItem->setPixmap( QPixmap( ":/IMG/IMG/Histogram.png" ) );
    m_pScene->setSceneRect( 0, 0, 10, 10 );

    //Fake no audio track
    paintAudioTrack();

    //And reset sliders
    on_actionResetReceipt_triggered();

    //Export not possible without mlv file
    ui->actionExport->setEnabled( false );
    ui->actionExportCurrentFrame->setEnabled( false );

    //Set Clip Info to Dialog
    m_pInfoDialog->ui->tableWidget->item( 0, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 1, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 2, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 3, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 4, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 5, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 6, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 7, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 8, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 9, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 10, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 11, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 12, 1 )->setText( "-" );
    m_pInfoDialog->ui->tableWidget->item( 13, 1 )->setText( "-" );

    ui->label_resResolution->setText( "0 x 0 pixels" );

    //Adapt slider to clip and move to position 0
    ui->horizontalSliderPosition->setValue( 0 );

    //Set label
    drawFrameNumberLabel();

    //If no clip loaded, import receipt is disabled
    ui->actionImportReceipt->setEnabled( false );
    //If no clip loaded, disable session save
    ui->actionSaveSession->setEnabled( false );
    ui->actionSaveAsSession->setEnabled( false );
    //Disable select all and delete clip actions
    ui->actionSelectAllClips->setEnabled( false );
    ui->actionDeleteSelectedClips->setEnabled( false );

    //Disable Gradient
    ui->checkBoxGradientEnable->setChecked( false );
    ui->checkBoxGradientEnable->setEnabled( false );
    ui->toolButtonGradientPaint->setEnabled( false );

    //Set darkframe subtraction, fix focus pixels and fix bad pixels to off
    ui->toolButtonDarkFrameSubtractionInt->setEnabled( false );
    setToolButtonDarkFrameSubtraction( 0 );
    setToolButtonFocusPixels( 0 );
    setToolButtonBadPixels( 0 );

    //Cut In & Out
    initCutInOut( -1 );

    //Draw TimeCode
    QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( 0, 25 ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
    pic.setDevicePixelRatio( devicePixelRatio() );
    m_pTcLabel->setPixmap( pic );

    //Reset session name
    m_sessionFileName.clear();
}

//returns true if file is already in session
bool MainWindow::isFileInSession(QString fileName)
{
    for( int i = 0; i < ui->listWidgetSession->count(); i++ )
    {
        if( ui->listWidgetSession->item( i )->toolTip() == fileName )
        {
            return true;
        }
    }
    return false;
}

//Set the edit sliders to settings
void MainWindow::setSliders(ReceiptSettings *receipt, bool paste)
{
    m_setSliders = true;
    ui->horizontalSliderExposure->setValue( receipt->exposure() );
    ui->horizontalSliderContrast->setValue( receipt->contrast() );
    if( receipt->temperature() == -1 )
    {
        //Init Temp read from the file when imported and loaded very first time completely
        setWhiteBalanceFromMlv( receipt );
    }
    ui->comboBoxUseCameraMatrix->setCurrentIndex( receipt->camMatrixUsed() );
    on_comboBoxUseCameraMatrix_currentIndexChanged( receipt->camMatrixUsed() );

    ui->horizontalSliderTemperature->setValue( receipt->temperature() );
    ui->horizontalSliderTint->setValue( receipt->tint() );
    on_horizontalSliderTint_valueChanged( receipt->tint() ); // Tint needs sometimes extra invitation
    ui->horizontalSliderClarity->setValue( receipt->clarity() );
    ui->horizontalSliderVibrance->setValue( receipt->vibrance() );
    ui->horizontalSliderSaturation->setValue( receipt->saturation() );

    ui->horizontalSliderDS->setValue( receipt->ds() );
    ui->horizontalSliderDR->setValue( receipt->dr() );
    ui->horizontalSliderLS->setValue( receipt->ls() );
    ui->horizontalSliderLR->setValue( receipt->lr() );

    ui->horizontalSliderLighten->setValue( receipt->lightening() );

    ui->horizontalSliderShadows->setValue( receipt->shadows() );
    ui->horizontalSliderHighlights->setValue( receipt->highlights() );

    ui->labelCurves->setConfiguration( receipt->gradationCurve() );
    ui->labelHueVsHue->setConfiguration( receipt->hueVsHue() );
    ui->labelHueVsSat->setConfiguration( receipt->hueVsSaturation() );
    ui->labelHueVsLuma->setConfiguration( receipt->hueVsLuminance() );
    ui->labelLumaVsSat->setConfiguration( receipt->lumaVsSaturation() );

    ui->checkBoxGradientEnable->setChecked( receipt->isGradientEnabled() );
    ui->horizontalSliderExposureGradient->setValue( receipt->gradientExposure() );
    ui->horizontalSliderContrastGradient->setValue( receipt->gradientContrast() );
    ui->spinBoxGradientX->setValue( receipt->gradientStartX() );
    ui->spinBoxGradientY->setValue( receipt->gradientStartY() );
    ui->dialGradientAngle->setValue( receipt->gradientAngle() );
    ui->spinBoxGradientLength->setValue( receipt->gradientLength() );

    ui->horizontalSliderSharpen->setValue( receipt->sharpen() );
    ui->horizontalSliderShMasking->setValue( receipt->shMasking() );
    ui->horizontalSliderChromaBlur->setValue( receipt->chromaBlur() );

    ui->checkBoxHighLightReconstruction->setChecked( receipt->isHighlightReconstruction() );
    on_checkBoxHighLightReconstruction_toggled( receipt->isHighlightReconstruction() );

    ui->checkBoxChromaSeparation->setChecked( receipt->isChromaSeparation() );
    on_checkBoxChromaSeparation_toggled( receipt->isChromaSeparation() );

    ui->comboBoxProfile->setCurrentIndex( receipt->profile() );
    on_comboBoxProfile_currentIndexChanged( receipt->profile() );
    if( receipt->tonemap() != -1 )
    {
        ui->comboBoxTonemapFct->setCurrentIndex( receipt->tonemap() );
        on_comboBoxTonemapFct_currentIndexChanged( receipt->tonemap() );
    }
    if( receipt->gamut() != -1 )
    {
        ui->comboBoxProcessingGamut->setCurrentIndex( receipt->gamut() );
        on_comboBoxProcessingGamut_currentIndexChanged( receipt->gamut() );
    }
    ui->horizontalSliderGamma->setValue( receipt->gamma() );

    ui->checkBoxCreativeAdjustments->setChecked( receipt->allowCreativeAdjustments() );
    on_checkBoxCreativeAdjustments_toggled( receipt->allowCreativeAdjustments() );

    ui->horizontalSliderDenoiseStrength->setValue( receipt->denoiserStrength() );
    ui->comboBoxDenoiseWindow->setCurrentIndex( receipt->denoiserWindow() - 2 );
    on_comboBoxDenoiseWindow_currentIndexChanged( receipt->denoiserWindow() - 2 );

    ui->horizontalSliderRbfDenoiseLuma->setValue( receipt->rbfDenoiserLuma() );
    ui->horizontalSliderRbfDenoiseChroma->setValue( receipt->rbfDenoiserChroma() );
    ui->horizontalSliderRbfDenoiseRange->setValue( receipt->rbfDenoiserRange() );

    ui->horizontalSliderGrainStrength->setValue( receipt->grainStrength() );

    ui->checkBoxRawFixEnable->setChecked( receipt->rawFixesEnabled() );
    on_checkBoxRawFixEnable_clicked( receipt->rawFixesEnabled() );
    if( receipt->focusPixels() == -1 )
    {
        //Init Focus Dot automatically when imported and loaded very first time completely
        setToolButtonFocusPixels( llrpDetectFocusDotFixMode( m_pMlvObject ) );
    }
    else
    {
        setToolButtonFocusPixels( receipt->focusPixels() );
    }
    setToolButtonFocusPixelsIntMethod( receipt->fpiMethod() );
    setToolButtonBadPixels( receipt->badPixels() );
    setToolButtonBadPixelsSearchMethod( receipt->bpsMethod() );
    setToolButtonBadPixelsIntMethod( receipt->bpiMethod() );
    setToolButtonChromaSmooth( receipt->chromaSmooth() );
    setToolButtonPatternNoise( receipt->patternNoise() );
    setToolButtonUpsideDown( receipt->upsideDown() );
    setToolButtonVerticalStripes( receipt->verticalStripes() );

    //Init
    if( receipt->dualIsoForced() == -1 )
    {
        receipt->setDualIsoForced( llrpGetDualIsoValidity( m_pMlvObject ) );
    }
    //Copy & Paste problems between old and new dual iso
    else if( receipt->dualIsoForced() == DISO_FORCED && llrpGetDualIsoValidity( m_pMlvObject ) == DISO_VALID )
    {
        receipt->setDualIsoForced( DISO_VALID );
    }
    //Copy & Paste problems between old and new dual iso
    else if( receipt->dualIsoForced() == DISO_VALID && llrpGetDualIsoValidity( m_pMlvObject ) == DISO_INVALID )
    {
        receipt->setDualIsoForced( DISO_FORCED );
    }
    ui->toolButtonDualIsoForce->setVisible( receipt->dualIsoForced() != DISO_VALID );
    ui->toolButtonDualIsoForce->setChecked( receipt->dualIsoForced() == DISO_FORCED );
    on_toolButtonDualIsoForce_toggled( receipt->dualIsoForced() == DISO_FORCED );

    setToolButtonDualIso( receipt->dualIso() );
    setToolButtonDualIsoInterpolation( receipt->dualIsoInterpolation() );
    setToolButtonDualIsoAliasMap( receipt->dualIsoAliasMap() );
    setToolButtonDualIsoFullresBlending( receipt->dualIsoFrBlending() );
    ui->spinBoxDeflickerTarget->setValue( receipt->deflickerTarget() );
    on_spinBoxDeflickerTarget_valueChanged( receipt->deflickerTarget() );
    ui->lineEditDarkFrameFile->setText( receipt->darkFrameFileName() );
    on_lineEditDarkFrameFile_textChanged( receipt->darkFrameFileName() );
    ui->toolButtonDarkFrameSubtractionInt->setEnabled( llrpGetDarkFrameIntStatus( m_pMlvObject ) );

    //Auto setup at first full import, else get from receipt
    if( receipt->darkFrameEnabled() == -1 )
    {
        if( llrpGetDarkFrameIntStatus( m_pMlvObject ) )
        {
            setToolButtonDarkFrameSubtraction( 2 );
        }
        else
        {
            setToolButtonDarkFrameSubtraction( 0 );
        }
    }
    else
    {
        setToolButtonDarkFrameSubtraction( receipt->darkFrameEnabled() );
    }

    ui->horizontalSliderTone->setValue( receipt->tone() );
    ui->horizontalSliderToningStrength->setValue( receipt->toningStrength() );

    ui->checkBoxLutEnable->setChecked( receipt->lutEnabled() );
    on_checkBoxLutEnable_clicked( receipt->lutEnabled() );
    ui->lineEditLutName->setText( receipt->lutName() );
    on_lineEditLutName_textChanged( receipt->lutName() );
    ui->horizontalSliderLutStrength->setValue( receipt->lutStrength() );

    ui->checkBoxFilterEnable->setChecked( receipt->filterEnabled() );
    on_checkBoxFilterEnable_clicked( receipt->filterEnabled() );
    ui->comboBoxFilterName->setCurrentIndex( receipt->filterIndex() );
    on_comboBoxFilterName_currentIndexChanged( receipt->filterIndex() );
    ui->horizontalSliderFilterStrength->setValue( receipt->filterStrength() );

    if( receipt->stretchFactorX() == STRETCH_H_100 ) ui->comboBoxHStretch->setCurrentIndex( 0 );
    else if( receipt->stretchFactorX() == STRETCH_H_133 ) ui->comboBoxHStretch->setCurrentIndex( 1 );
    else if( receipt->stretchFactorX() == STRETCH_H_150 ) ui->comboBoxHStretch->setCurrentIndex( 2 );
    else if( receipt->stretchFactorX() == STRETCH_H_175 ) ui->comboBoxHStretch->setCurrentIndex( 3 );
    else if( receipt->stretchFactorX() == STRETCH_H_180 ) ui->comboBoxHStretch->setCurrentIndex( 4 );
    else ui->comboBoxHStretch->setCurrentIndex( 5 );
    on_comboBoxHStretch_currentIndexChanged( ui->comboBoxHStretch->currentIndex() );

    if( receipt->stretchFactorY() == -1 )
    {
        float ratioV = getMlvAspectRatio( m_pMlvObject );
        if( ratioV == 0.0 ) ratioV = 1.0; // set it to 1 if no information in the MLV file
        //Init vertical stretching automatically when imported and loaded very first time completely
        if( ratioV > 0.9 && ratioV < 1.1 ) ui->comboBoxVStretch->setCurrentIndex( 0 );
        else if( ratioV > 1.6 && ratioV < 1.7 ) ui->comboBoxVStretch->setCurrentIndex( 1 );
        else if( ratioV > 2.9 && ratioV < 3.1 ) ui->comboBoxVStretch->setCurrentIndex( 2 );
        else ui->comboBoxVStretch->setCurrentIndex( 3 );
    }
    else if( receipt->stretchFactorY() == STRETCH_V_100 ) ui->comboBoxVStretch->setCurrentIndex( 0 );
    else if( receipt->stretchFactorY() == STRETCH_V_167 ) ui->comboBoxVStretch->setCurrentIndex( 1 );
    else if( receipt->stretchFactorY() == STRETCH_V_300 ) ui->comboBoxVStretch->setCurrentIndex( 2 );
    else ui->comboBoxVStretch->setCurrentIndex( 3 );
    on_comboBoxVStretch_currentIndexChanged( ui->comboBoxVStretch->currentIndex() );

    //Vignette after stretching in order to use stretching once only
    ui->horizontalSliderVignetteStrength->setValue( receipt->vignetteStrength() );
    ui->horizontalSliderVignetteShape->blockSignals( true );
    ui->horizontalSliderVignetteShape->setValue( receipt->vignetteShape() );
    ui->horizontalSliderVignetteShape->blockSignals( false );
    ui->horizontalSliderVignetteRadius->blockSignals( true );
    ui->horizontalSliderVignetteRadius->setValue( receipt->vignetteRadius() );
    ui->horizontalSliderVignetteRadius->blockSignals( false );
    on_horizontalSliderVignetteRadius_valueChanged( receipt->vignetteRadius() );
    ui->horizontalSliderCaRed->setValue( receipt->caRed() );
    on_horizontalSliderCaRed_valueChanged( receipt->caRed() );
    ui->horizontalSliderCaBlue->setValue( receipt->caBlue() );
    on_horizontalSliderCaBlue_valueChanged( receipt->caBlue() );
    ui->horizontalSliderCaDesaturate->setValue( receipt->caDesaturate() );
    on_horizontalSliderCaDesaturate_valueChanged( receipt->caDesaturate() );
    ui->horizontalSliderCaRadius->setValue( receipt->caRadius() );
    on_horizontalSliderCaRadius_valueChanged( receipt->caRadius() );

    if( !paste && !receipt->wasNeverLoaded() )
    {
        ui->spinBoxCutIn->setValue( receipt->cutIn() );
        on_spinBoxCutIn_valueChanged( receipt->cutIn() );
        ui->spinBoxCutOut->setValue( receipt->cutOut() );
        on_spinBoxCutOut_valueChanged( receipt->cutOut() );
    }

    if( receipt->rawBlack() != -1 )
    {
        ui->horizontalSliderRawBlack->setValue( receipt->rawBlack() );
    }
    if( receipt->rawWhite() != -1 )
    {
        ui->horizontalSliderRawWhite->setValue( receipt->rawWhite() );
    }

    m_pMlvObject->current_cached_frame_active = 0;

    if( ui->actionPlaybackPosition->isChecked() ) ui->horizontalSliderPosition->setValue( receipt->lastPlaybackPosition() );
    ui->comboBoxDebayer->setCurrentIndex( receipt->debayer() );
    on_comboBoxDebayer_currentIndexChanged( receipt->debayer() );

    ui->checkBoxVidstabEnable->setChecked( receipt->vidStabEnabled() );
    on_checkBoxVidstabEnable_toggled( receipt->vidStabEnabled() );
    ui->horizontalSliderVidstabStepsize->setValue( receipt->vidStabStepsize() );
    ui->horizontalSliderVidstabShakiness->setValue( receipt->vidStabShakiness() );
    ui->horizontalSliderVidstabAccuracy->setValue( receipt->vidStabAccuracy() );
    ui->horizontalSliderVidstabZoom->setValue( receipt->vidStabZoom() );
    ui->horizontalSliderVidstabSmoothing->setValue( receipt->vidStabSmoothing() );
    ui->checkBoxVidstabTripod->setChecked( receipt->vidStabTripod() );
    on_checkBoxVidstabTripod_toggled( receipt->vidStabTripod() );

    m_setSliders = false;
}

//Set the receipt from sliders
void MainWindow::setReceipt( ReceiptSettings *receipt )
{
    receipt->setExposure( ui->horizontalSliderExposure->value() );
    receipt->setContrast( ui->horizontalSliderContrast->value() );
    receipt->setTemperature( ui->horizontalSliderTemperature->value() );
    receipt->setTint( ui->horizontalSliderTint->value() );
    receipt->setClarity( ui->horizontalSliderClarity->value() );
    receipt->setVibrance( ui->horizontalSliderVibrance->value() );
    receipt->setSaturation( ui->horizontalSliderSaturation->value() );
    receipt->setDs( ui->horizontalSliderDS->value() );
    receipt->setDr( ui->horizontalSliderDR->value() );
    receipt->setLs( ui->horizontalSliderLS->value() );
    receipt->setLr( ui->horizontalSliderLR->value() );
    receipt->setLightening( ui->horizontalSliderLighten->value() );
    receipt->setShadows( ui->horizontalSliderShadows->value() );
    receipt->setHighlights( ui->horizontalSliderHighlights->value() );
    receipt->setGradationCurve( ui->labelCurves->configuration() );
    receipt->setHueVsHue( ui->labelHueVsHue->configuration() );
    receipt->setHueVsSaturation( ui->labelHueVsSat->configuration() );
    receipt->setHueVsLuminance( ui->labelHueVsLuma->configuration() );
    receipt->setLumaVsSaturation( ui->labelLumaVsSat->configuration() );

    receipt->setGradientEnabled( ui->checkBoxGradientEnable->isChecked() );
    receipt->setGradientExposure( ui->horizontalSliderExposureGradient->value() );
    receipt->setGradientContrast( ui->horizontalSliderContrastGradient->value() );
    receipt->setGradientStartX( ui->spinBoxGradientX->value() );
    receipt->setGradientStartY( ui->spinBoxGradientY->value() );
    receipt->setGradientLength( ui->spinBoxGradientLength->value() );
    receipt->setGradientAngle( ui->dialGradientAngle->value() );

    receipt->setSharpen( ui->horizontalSliderSharpen->value() );
    receipt->setShMasking( ui->horizontalSliderShMasking->value() );
    receipt->setChromaBlur( ui->horizontalSliderChromaBlur->value() );
    receipt->setHighlightReconstruction( ui->checkBoxHighLightReconstruction->isChecked() );
    receipt->setCamMatrixUsed( ui->comboBoxUseCameraMatrix->currentIndex() );
    receipt->setChromaSeparation( ui->checkBoxChromaSeparation->isChecked() );
    receipt->setProfile( ui->comboBoxProfile->currentIndex() );
    receipt->setTonemap( ui->comboBoxTonemapFct->currentIndex() );
    receipt->setGamut( ui->comboBoxProcessingGamut->currentIndex() );
    receipt->setGamma( ui->horizontalSliderGamma->value() );
    receipt->setAllowCreativeAdjustments( ui->checkBoxCreativeAdjustments->isChecked() );
    receipt->setDenoiserStrength( ui->horizontalSliderDenoiseStrength->value() );
    receipt->setDenoiserWindow( ui->comboBoxDenoiseWindow->currentIndex() + 2 );
    receipt->setRbfDenoiserLuma( ui->horizontalSliderRbfDenoiseLuma->value() );
    receipt->setRbfDenoiserChroma( ui->horizontalSliderRbfDenoiseChroma->value() );
    receipt->setRbfDenoiserRange( ui->horizontalSliderRbfDenoiseRange->value() );
    receipt->setGrainStrength( ui->horizontalSliderGrainStrength->value() );

    receipt->setRawFixesEnabled( ui->checkBoxRawFixEnable->isChecked() );
    receipt->setVerticalStripes( toolButtonVerticalStripesCurrentIndex() );
    receipt->setFocusPixels( toolButtonFocusPixelsCurrentIndex() );
    receipt->setFpiMethod( toolButtonFocusPixelsIntMethodCurrentIndex() );
    receipt->setBadPixels( toolButtonBadPixelsCurrentIndex() );
    receipt->setBpsMethod( toolButtonBadPixelsSearchMethodCurrentIndex() );
    receipt->setBpiMethod( toolButtonBadPixelsIntMethodCurrentIndex() );
    receipt->setChromaSmooth( toolButtonChromaSmoothCurrentIndex() );
    receipt->setPatternNoise( toolButtonPatternNoiseCurrentIndex() );
    receipt->setUpsideDown( toolButtonUpsideDownCurrentIndex() );
    receipt->setDeflickerTarget( ui->spinBoxDeflickerTarget->value() );
    receipt->setDualIsoForced( llrpGetDualIsoValidity( m_pMlvObject ) );
    receipt->setDualIso( toolButtonDualIsoCurrentIndex() );
    receipt->setDualIsoInterpolation( toolButtonDualIsoInterpolationCurrentIndex() );
    receipt->setDualIsoAliasMap( toolButtonDualIsoAliasMapCurrentIndex() );
    receipt->setDualIsoFrBlending( toolButtonDualIsoFullresBlendingCurrentIndex() );
    receipt->setDualIsoWhite( processingGetWhiteLevel( m_pMlvObject->processing ) );
    receipt->setDualIsoBlack( processingGetBlackLevel( m_pMlvObject->processing ) );
    receipt->setDarkFrameFileName( ui->lineEditDarkFrameFile->text() );
    receipt->setDarkFrameEnabled( toolButtonDarkFrameSubtractionCurrentIndex() );
    receipt->setRawBlack( ui->horizontalSliderRawBlack->value() );
    receipt->setRawWhite( ui->horizontalSliderRawWhite->value() );

    receipt->setTone( ui->horizontalSliderTone->value() );
    receipt->setToningStrength( ui->horizontalSliderToningStrength->value() );

    receipt->setLutEnabled( ui->checkBoxLutEnable->isChecked() );
    receipt->setLutName( ui->lineEditLutName->text() );
    receipt->setLutStrength( ui->horizontalSliderLutStrength->value() );

    receipt->setFilterEnabled( ui->checkBoxFilterEnable->isChecked() );
    receipt->setFilterIndex( ui->comboBoxFilterName->currentIndex() );
    receipt->setFilterStrength( ui->horizontalSliderFilterStrength->value() );

    receipt->setVignetteStrength( ui->horizontalSliderVignetteStrength->value() );
    receipt->setVignetteRadius( ui->horizontalSliderVignetteRadius->value() );
    receipt->setVignetteShape( ui->horizontalSliderVignetteShape->value() );
    receipt->setCaRed( ui->horizontalSliderCaRed->value() );
    receipt->setCaBlue( ui->horizontalSliderCaBlue->value() );
    receipt->setCaDesaturate( ui->horizontalSliderCaDesaturate->value() );
    receipt->setCaRadius( ui->horizontalSliderCaRadius->value() );

    receipt->setStretchFactorX( getHorizontalStretchFactor(true) );
    receipt->setStretchFactorY( getVerticalStretchFactor(true) );

    receipt->setCutIn( ui->spinBoxCutIn->value() );
    receipt->setCutOut( ui->spinBoxCutOut->value() );

    if( ui->actionPlaybackPosition->isChecked() ) receipt->setLastPlaybackPosition( ui->horizontalSliderPosition->value() );
    else receipt->setLastPlaybackPosition( 0 );

    receipt->setDebayer( ui->comboBoxDebayer->currentIndex() );

    receipt->setVidstabEnabled( ui->checkBoxVidstabEnable->isChecked() );
    receipt->setVidstabStepsize( ui->horizontalSliderVidstabStepsize->value() );
    receipt->setVidstabShakiness( ui->horizontalSliderVidstabShakiness->value() );
    receipt->setVidstabAccuracy( ui->horizontalSliderVidstabAccuracy->value() );
    receipt->setVidstabZoom( ui->horizontalSliderVidstabZoom->value() );
    receipt->setVidstabSmoothing( ui->horizontalSliderVidstabSmoothing->value() );
    receipt->setVidstabTripod( ui->checkBoxVidstabTripod->isChecked() );
}

//Replace receipt settings
void MainWindow::replaceReceipt(ReceiptSettings *receiptTarget, ReceiptSettings *receiptSource, bool paste)
{
    Ui::ReceiptCopyMaskDialog *cdui = m_pCopyMask->ui;

    if( paste && cdui->checkBoxExposure->isChecked() )   receiptTarget->setExposure( receiptSource->exposure() );
    if( paste && cdui->checkBoxContrast->isChecked() )   receiptTarget->setContrast( receiptSource->contrast() );
    if( paste && cdui->checkBoxWb->isChecked() )         receiptTarget->setTemperature( receiptSource->temperature() );
    if( paste && cdui->checkBoxWb->isChecked() )         receiptTarget->setTint( receiptSource->tint() );
    if( paste && cdui->checkBoxClarity->isChecked() )    receiptTarget->setClarity( receiptSource->clarity() );
    if( paste && cdui->checkBoxVibrance->isChecked() )   receiptTarget->setVibrance( receiptSource->vibrance() );
    if( paste && cdui->checkBoxSaturation->isChecked() ) receiptTarget->setSaturation( receiptSource->saturation() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setDs( receiptSource->ds() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setDr( receiptSource->dr() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setLs( receiptSource->ls() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setLr( receiptSource->lr() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setLightening( receiptSource->lightening() );
    if( paste && cdui->checkBoxGradationCurve->isChecked() ) receiptTarget->setGradationCurve( receiptSource->gradationCurve() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setHueVsHue( receiptSource->hueVsHue() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setHueVsSaturation( receiptSource->hueVsSaturation() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setHueVsLuminance( receiptSource->hueVsLuminance() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setLumaVsSaturation( receiptSource->lumaVsSaturation() );
    if( paste && cdui->checkBoxShadows->isChecked() )    receiptTarget->setShadows( receiptSource->shadows() );
    if( paste && cdui->checkBoxHighlights->isChecked() ) receiptTarget->setHighlights( receiptSource->highlights() );

    if( paste && cdui->checkBoxGradient->isChecked() )
    {
        receiptTarget->setGradientEnabled( receiptSource->isGradientEnabled() );
        receiptTarget->setGradientExposure( receiptSource->gradientExposure() );
        receiptTarget->setGradientContrast( receiptSource->gradientContrast() );
        receiptTarget->setGradientStartX( receiptSource->gradientStartX() );
        receiptTarget->setGradientStartY( receiptSource->gradientStartY() );
        receiptTarget->setGradientLength( receiptSource->gradientLength() );
        receiptTarget->setGradientAngle( receiptSource->gradientAngle() );
    }

    if( paste && cdui->checkBoxSharpen->isChecked() )
    {
        receiptTarget->setSharpen( receiptSource->sharpen() );
        receiptTarget->setShMasking( receiptSource->shMasking() );
    }
    if( paste && cdui->checkBoxChromaBlur->isChecked() ) receiptTarget->setChromaBlur( receiptSource->chromaBlur() );
    if( paste && cdui->checkBoxHighlightReconstruction->isChecked() ) receiptTarget->setHighlightReconstruction( receiptSource->isHighlightReconstruction() );
    if( paste && cdui->checkBoxCameraMatrix->isChecked() ) receiptTarget->setCamMatrixUsed( receiptSource->camMatrixUsed() );
    if( paste && cdui->checkBoxChromaBlur->isChecked() ) receiptTarget->setChromaSeparation( receiptSource->isChromaSeparation() );
    if( paste && cdui->checkBoxProfile->isChecked() )
    {
        receiptTarget->setProfile( receiptSource->profile() );
        receiptTarget->setAllowCreativeAdjustments( receiptSource->allowCreativeAdjustments() );
        receiptTarget->setTonemap( receiptSource->tonemap() );
        receiptTarget->setGamut( receiptSource->gamut() );
        receiptTarget->setGamma( receiptSource->gamma() );
    }
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setDenoiserStrength( receiptSource->denoiserStrength() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setDenoiserWindow( receiptSource->denoiserWindow() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setRbfDenoiserLuma( receiptSource->rbfDenoiserLuma() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setRbfDenoiserChroma( receiptSource->rbfDenoiserChroma() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setRbfDenoiserRange( receiptSource->rbfDenoiserRange() );
    if( paste && cdui->checkBoxGrain->isChecked() )      receiptTarget->setGrainStrength( receiptSource->grainStrength() );

    if( paste && cdui->checkBoxRawCorrectEnable->isChecked() ) receiptTarget->setRawFixesEnabled( receiptSource->rawFixesEnabled() );
    if( paste && cdui->checkBoxDarkFrameSubtraction->isChecked() ) receiptTarget->setDarkFrameFileName( receiptSource->darkFrameFileName() );
    if( paste && cdui->checkBoxDarkFrameSubtraction->isChecked() ) receiptTarget->setDarkFrameEnabled( receiptSource->darkFrameEnabled() );
    if( paste && cdui->checkBoxVerticalStripes->isChecked() )  receiptTarget->setVerticalStripes( receiptSource->verticalStripes() );
    if( paste && cdui->checkBoxFoxusDots->isChecked() )        receiptTarget->setFocusPixels( receiptSource->focusPixels() );
    if( paste && cdui->checkBoxFoxusDots->isChecked() )        receiptTarget->setFpiMethod( receiptSource->fpiMethod() );
    if( paste && cdui->checkBoxBadPixels->isChecked() )        receiptTarget->setBadPixels( receiptSource->badPixels() );
    if( paste && cdui->checkBoxBadPixels->isChecked() )        receiptTarget->setBpsMethod( receiptSource->bpsMethod() );
    if( paste && cdui->checkBoxBadPixels->isChecked() )        receiptTarget->setBpiMethod( receiptSource->bpiMethod() );
    if( paste && cdui->checkBoxChromaSmooth->isChecked() )     receiptTarget->setChromaSmooth( receiptSource->chromaSmooth() );
    if( paste && cdui->checkBoxPatternNoise->isChecked() )     receiptTarget->setPatternNoise( receiptSource->patternNoise() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoForced( receiptSource->dualIsoForced() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIso( receiptSource->dualIso() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoInterpolation( receiptSource->dualIsoInterpolation() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoAliasMap( receiptSource->dualIsoAliasMap() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoFrBlending( receiptSource->dualIsoFrBlending() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoWhite( receiptSource->dualIsoWhite() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoBlack( receiptSource->dualIsoBlack() );
    if( paste && cdui->checkBoxRawBlackLevel->isChecked() )    receiptTarget->setRawBlack( receiptSource->rawBlack() );
    if( paste && cdui->checkBoxRawWhiteLevel->isChecked() )    receiptTarget->setRawWhite( receiptSource->rawWhite() );

    receiptTarget->setDeflickerTarget( receiptSource->deflickerTarget() );

    if( paste && cdui->checkBoxDebayer->isChecked() )          receiptTarget->setDebayer( receiptSource->debayer() );

    if( paste )
    {
        receiptTarget->setTone( receiptSource->tone() );
        receiptTarget->setToningStrength( receiptSource->toningStrength() );
    }

    if( paste && cdui->checkBoxLut->isChecked() )
    {
        receiptTarget->setLutEnabled( receiptSource->lutEnabled() );
        receiptTarget->setLutName( receiptSource->lutName() );
        receiptTarget->setLutStrength( receiptSource->lutStrength() );
    }

    if( paste && cdui->checkBoxFilter->isChecked() )
    {
        receiptTarget->setFilterEnabled( receiptSource->filterEnabled() );
        receiptTarget->setFilterIndex( receiptSource->filterIndex() );
        receiptTarget->setFilterStrength( receiptSource->filterStrength() );
    }
    if( paste && cdui->checkBoxVignette->isChecked() )
    {
        receiptTarget->setVignetteStrength( receiptSource->vignetteStrength() );
        receiptTarget->setVignetteRadius( receiptSource->vignetteRadius() );
        receiptTarget->setVignetteShape( receiptSource->vignetteShape() );
        receiptTarget->setCaRed( receiptSource->caRed() );
        receiptTarget->setCaBlue( receiptSource->caBlue() );
        receiptTarget->setCaDesaturate( receiptSource->caDesaturate() );
        receiptTarget->setCaRadius( receiptSource->caRadius() );
    }

    if( paste && cdui->checkBoxTransformation->isChecked() )
    {
        receiptTarget->setStretchFactorX( receiptSource->stretchFactorX() );
        receiptTarget->setStretchFactorY( receiptSource->stretchFactorY() );
        receiptTarget->setUpsideDown( receiptSource->upsideDown() );
        receiptTarget->setVidstabEnabled( receiptSource->vidStabEnabled() );
        receiptTarget->setVidstabStepsize( receiptSource->vidStabStepsize() );
        receiptTarget->setVidstabShakiness( receiptSource->vidStabShakiness() );
        receiptTarget->setVidstabAccuracy( receiptSource->vidStabAccuracy() );
        receiptTarget->setVidstabZoom( receiptSource->vidStabZoom() );
        receiptTarget->setVidstabSmoothing( receiptSource->vidStabSmoothing() );
        receiptTarget->setVidstabTripod( receiptSource->vidStabTripod() );
    }

    if( !paste )
    {
        receiptTarget->setCutIn( receiptSource->cutIn() );
        receiptTarget->setCutOut( receiptSource->cutOut() );
    }
}

//Show the file in
int MainWindow::showFileInEditor(int row)
{
    if( m_pSessionReceipts.count() <= 0 ) return 1;

    //Stop Playback
    ui->actionPlay->setChecked( false );
    //Save slider receipt
    if( !m_pSessionReceipts.at( m_lastActiveClipInSession )->wasNeverLoaded() ) setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );
    //Open new MLV
    if( openMlv( ui->listWidgetSession->item( row )->toolTip() ) )
    {
        //If one file is selected, reselect the last one, else do nothing (export)
        //And if there is another file we can switch to...
        if( ui->listWidgetSession->selectedItems().count() <= 1
         && ui->listWidgetSession->count() > 1)
        {
            ui->listWidgetSession->setCurrentRow( m_lastActiveClipInSession );
            showFileInEditor( m_lastActiveClipInSession );
        }
        return 1;
    }
    //Now set it was loaded once
    m_pSessionReceipts.at( row )->setLoaded();
    //Set sliders to receipt
    setSliders( m_pSessionReceipts.at( row ), false );
    //Save new position in session
    m_lastActiveClipInSession = row;

    //Caching is in which state? Set it!
    if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();

    return 0;
}

//Add the clip in SessionList position "row" at last position in ExportQueue
void MainWindow::addClipToExportQueue(int row, QString fileName)
{
    //A file must be opened once before being able to be exported
    if( m_pSessionReceipts.at( row )->wasNeverLoaded() )
    {
        m_pStatusDialog->ui->label->setText( "Preparing export..." );
        m_pStatusDialog->ui->labelEstimatedTime->setText( "" );
        m_pStatusDialog->ui->progressBar->setValue( 0 );
        m_pStatusDialog->open();
        if( showFileInEditor( row ) ) return; //Don't add to export queue when corrupted file
        qApp->processEvents();
        setReceipt( m_pSessionReceipts.at( row ) );
    }

    ReceiptSettings *receipt = new ReceiptSettings();
    receipt->setExposure( m_pSessionReceipts.at( row )->exposure() );
    receipt->setContrast( m_pSessionReceipts.at( row )->contrast() );
    receipt->setTemperature( m_pSessionReceipts.at( row )->temperature() );
    receipt->setTint( m_pSessionReceipts.at( row )->tint() );
    receipt->setClarity( m_pSessionReceipts.at( row )->clarity() );
    receipt->setVibrance( m_pSessionReceipts.at( row )->vibrance() );
    receipt->setSaturation( m_pSessionReceipts.at( row )->saturation() );
    receipt->setDr( m_pSessionReceipts.at( row )->dr() );
    receipt->setDs( m_pSessionReceipts.at( row )->ds() );
    receipt->setLr( m_pSessionReceipts.at( row )->lr() );
    receipt->setLs( m_pSessionReceipts.at( row )->ls() );
    receipt->setLightening( m_pSessionReceipts.at( row )->lightening() );
    receipt->setShadows( m_pSessionReceipts.at( row )->shadows() );
    receipt->setHighlights( m_pSessionReceipts.at( row )->highlights() );
    receipt->setGradationCurve( m_pSessionReceipts.at( row )->gradationCurve() );
    receipt->setHueVsHue( m_pSessionReceipts.at( row )->hueVsHue() );
    receipt->setHueVsSaturation( m_pSessionReceipts.at( row )->hueVsSaturation() );
    receipt->setHueVsLuminance( m_pSessionReceipts.at( row )->hueVsLuminance() );
    receipt->setLumaVsSaturation( m_pSessionReceipts.at( row )->lumaVsSaturation() );

    receipt->setGradientEnabled( m_pSessionReceipts.at( row )->isGradientEnabled() );
    receipt->setGradientExposure( m_pSessionReceipts.at( row )->gradientExposure() );
    receipt->setGradientContrast( m_pSessionReceipts.at( row )->gradientContrast() );
    receipt->setGradientStartX( m_pSessionReceipts.at( row )->gradientStartX() );
    receipt->setGradientStartY( m_pSessionReceipts.at( row )->gradientStartY() );
    receipt->setGradientLength( m_pSessionReceipts.at( row )->gradientLength() );
    receipt->setGradientAngle( m_pSessionReceipts.at( row )->gradientAngle() );

    receipt->setSharpen( m_pSessionReceipts.at( row )->sharpen() );
    receipt->setShMasking( m_pSessionReceipts.at( row )->shMasking() );
    receipt->setChromaBlur( m_pSessionReceipts.at( row )->chromaBlur() );
    receipt->setHighlightReconstruction( m_pSessionReceipts.at( row )->isHighlightReconstruction() );
    receipt->setCamMatrixUsed( m_pSessionReceipts.at( row )->camMatrixUsed() );
    receipt->setChromaSeparation( m_pSessionReceipts.at( row )->isChromaSeparation() );
    receipt->setProfile( m_pSessionReceipts.at( row )->profile() );
    receipt->setAllowCreativeAdjustments( m_pSessionReceipts.at( row )->allowCreativeAdjustments() );
    receipt->setTonemap( m_pSessionReceipts.at( row )->tonemap() );
    receipt->setGamut( m_pSessionReceipts.at( row )->gamut() );
    receipt->setGamma( m_pSessionReceipts.at( row )->gamma() );
    receipt->setDenoiserStrength( m_pSessionReceipts.at( row )->denoiserStrength() );
    receipt->setDenoiserWindow( m_pSessionReceipts.at( row )->denoiserWindow() );
    receipt->setRbfDenoiserLuma( m_pSessionReceipts.at( row )->rbfDenoiserLuma() );
    receipt->setRbfDenoiserChroma( m_pSessionReceipts.at( row )->rbfDenoiserChroma() );
    receipt->setRbfDenoiserRange( m_pSessionReceipts.at( row )->rbfDenoiserRange() );
    receipt->setGrainStrength( m_pSessionReceipts.at( row )->grainStrength() );

    receipt->setRawFixesEnabled( m_pSessionReceipts.at( row )->rawFixesEnabled() );
    receipt->setVerticalStripes( m_pSessionReceipts.at( row )->verticalStripes() );
    receipt->setFocusPixels( m_pSessionReceipts.at( row )->focusPixels() );
    receipt->setFpiMethod( m_pSessionReceipts.at( row )->fpiMethod() );
    receipt->setBadPixels( m_pSessionReceipts.at( row )->badPixels() );
    receipt->setBpsMethod( m_pSessionReceipts.at( row )->bpsMethod() );
    receipt->setBpiMethod( m_pSessionReceipts.at( row )->bpiMethod() );
    receipt->setChromaSmooth( m_pSessionReceipts.at( row )->chromaSmooth() );
    receipt->setPatternNoise( m_pSessionReceipts.at( row )->patternNoise() );
    receipt->setDeflickerTarget( m_pSessionReceipts.at( row )->deflickerTarget() );
    receipt->setDualIsoForced( m_pSessionReceipts.at( row )->dualIsoForced() );
    receipt->setDualIso( m_pSessionReceipts.at( row )->dualIso() );
    receipt->setDualIsoInterpolation( m_pSessionReceipts.at( row )->dualIsoInterpolation() );
    receipt->setDualIsoAliasMap( m_pSessionReceipts.at( row )->dualIsoAliasMap() );
    receipt->setDualIsoFrBlending( m_pSessionReceipts.at( row )->dualIsoFrBlending() );
    receipt->setDualIsoWhite( m_pSessionReceipts.at( row )->dualIsoWhite() );
    receipt->setDualIsoBlack( m_pSessionReceipts.at( row )->dualIsoBlack() );
    receipt->setDarkFrameFileName( m_pSessionReceipts.at( row )->darkFrameFileName() );
    receipt->setDarkFrameEnabled( m_pSessionReceipts.at( row )->darkFrameEnabled() );
    receipt->setRawBlack( m_pSessionReceipts.at( row )->rawBlack() );
    receipt->setRawWhite( m_pSessionReceipts.at( row )->rawWhite() );

    receipt->setTone( m_pSessionReceipts.at( row )->tone() );
    receipt->setToningStrength( m_pSessionReceipts.at( row )->toningStrength() );

    receipt->setLutEnabled( m_pSessionReceipts.at( row )->lutEnabled() );
    receipt->setLutName( m_pSessionReceipts.at( row )->lutName() );
    receipt->setLutStrength( m_pSessionReceipts.at( row )->lutStrength() );

    receipt->setFilterEnabled( m_pSessionReceipts.at( row )->filterEnabled() );
    receipt->setFilterIndex( m_pSessionReceipts.at( row )->filterIndex() );
    receipt->setFilterStrength( m_pSessionReceipts.at( row )->filterStrength() );

    receipt->setVignetteStrength( m_pSessionReceipts.at( row )->vignetteStrength() );
    receipt->setVignetteRadius( m_pSessionReceipts.at( row )->vignetteRadius() );
    receipt->setVignetteShape( m_pSessionReceipts.at( row )->vignetteShape() );
    receipt->setCaRed( m_pSessionReceipts.at( row )->caRed() );
    receipt->setCaBlue( m_pSessionReceipts.at( row )->caBlue() );
    receipt->setCaDesaturate( m_pSessionReceipts.at( row )->caDesaturate() );
    receipt->setCaRadius( m_pSessionReceipts.at( row )->caRadius() );

    receipt->setStretchFactorX( m_pSessionReceipts.at( row )->stretchFactorX() );
    receipt->setStretchFactorY( m_pSessionReceipts.at( row )->stretchFactorY() );
    receipt->setUpsideDown( m_pSessionReceipts.at( row )->upsideDown() );
    receipt->setVidstabEnabled( m_pSessionReceipts.at( row )->vidStabEnabled() );
    receipt->setVidstabStepsize( m_pSessionReceipts.at( row )->vidStabStepsize() );
    receipt->setVidstabShakiness( m_pSessionReceipts.at( row )->vidStabShakiness() );
    receipt->setVidstabAccuracy( m_pSessionReceipts.at( row )->vidStabAccuracy() );
    receipt->setVidstabZoom( m_pSessionReceipts.at( row )->vidStabZoom() );
    receipt->setVidstabSmoothing( m_pSessionReceipts.at( row )->vidStabSmoothing() );
    receipt->setVidstabTripod( m_pSessionReceipts.at( row )->vidStabTripod() );

    receipt->setDebayer( m_pSessionReceipts.at( row )->debayer() );

    receipt->setFileName( m_pSessionReceipts.at( row )->fileName() );
    receipt->setCutIn( m_pSessionReceipts.at( row )->cutIn() );
    receipt->setCutOut( m_pSessionReceipts.at( row )->cutOut() );
    receipt->setExportFileName( fileName );
    m_exportQueue.append( receipt );
}

//Handles preview pictures - make sure that right clip for row is loaded before!
void MainWindow::previewPicture( int row )
{
    //disable low level raw fixes for preview
    m_pMlvObject->llrawproc->fix_raw = 0;

    //Get frame from library
    getMlvProcessedFrame8( m_pMlvObject, 0, m_pRawImage, QThread::idealThreadCount() );

    //Display in SessionList
    QPixmap pic = QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                                      .scaled( getMlvWidth(m_pMlvObject) * devicePixelRatio() / 10.0 * getHorizontalStretchFactor(true),
                                               getMlvHeight(m_pMlvObject) * devicePixelRatio() / 10.0 * getVerticalStretchFactor(true),
                                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
    pic.setDevicePixelRatio( devicePixelRatio() );
    //Take and insert item, so the scrollbar is set correctly to the size of the picture
    QListWidgetItem *item = ui->listWidgetSession->takeItem( row );
    item->setIcon( QIcon( pic ) );
    ui->listWidgetSession->insertItem( row, item );
    //And select it
    ui->listWidgetSession->setCurrentRow( row );
    setPreviewMode();
}

//Sets the preview mode
void MainWindow::setPreviewMode( void )
{
    if( m_previewMode == 1 )
    {
        ui->listWidgetSession->setViewMode( QListView::ListMode );
        ui->listWidgetSession->setIconSize( QSize( 50, 30 ) );
        ui->listWidgetSession->setGridSize( QSize( -1, -1 ) );
        ui->listWidgetSession->setAlternatingRowColors( true );
        ui->listWidgetSession->setResizeMode( QListView::Fixed );
        ui->listWidgetSession->setFlow( QListView::TopToBottom );
        ui->listWidgetSession->setWrapping( false );
    }
    else if( m_previewMode == 2 || m_previewMode == 3 )
    {
        ui->listWidgetSession->setViewMode( QListView::IconMode );
        ui->listWidgetSession->setIconSize( QSize( 130, 80 ) );
        ui->listWidgetSession->setGridSize( QSize( 140, 100 ) );
        ui->listWidgetSession->setAlternatingRowColors( false );
        ui->listWidgetSession->setResizeMode( QListView::Adjust );
        ui->listWidgetSession->setFlow( QListView::LeftToRight );
        ui->listWidgetSession->setWrapping( true );
    }
    else
    {
        ui->listWidgetSession->setViewMode( QListView::ListMode );
        ui->listWidgetSession->setIconSize( QSize( 0, 0 ) );
        ui->listWidgetSession->setGridSize( QSize( -1, -1 ) );
        ui->listWidgetSession->setAlternatingRowColors( true );
        ui->listWidgetSession->setResizeMode( QListView::Fixed );
        ui->listWidgetSession->setFlow( QListView::TopToBottom );
        ui->listWidgetSession->setWrapping( false );
    }
}

//Get the framerate. Override or Original
double MainWindow::getFramerate( void )
{
    if( m_fpsOverride ) return m_frameRate;
    else return getMlvFramerate( m_pMlvObject );
}

//Paint the Audio Track Wave to GUI
void MainWindow::paintAudioTrack( void )
{
    QPixmap pic;
    //Fake graphic if nothing is loaded
    if( !m_fileLoaded )
    {
        pic = QPixmap::fromImage( m_pAudioWave->getMonoWave( NULL, 0, ui->labelAudioTrack->width(), devicePixelRatio() ) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        ui->labelAudioTrack->setPixmap( pic );
        ui->labelAudioTrack->setEnabled( false );
        ui->labelAudioTrack->setMinimumSize( 1, 1 ); //Otherwise window won't be smaller than picture
        ui->labelAudioTrack->setAlignment( Qt::AlignCenter ); //Always in the middle
        return;
    }
    //Make it disabled if clip has no audio
    ui->labelAudioTrack->setEnabled( doesMlvHaveAudio( m_pMlvObject ) );
    //Also fake graphic if no audio in clip
    if( !doesMlvHaveAudio( m_pMlvObject ) )
    {
        pic = QPixmap::fromImage( m_pAudioWave->getMonoWave( NULL, 0, ui->labelAudioTrack->width(), devicePixelRatio() ) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        ui->labelAudioTrack->setPixmap( pic );
    }
    //Load audio data and paint
    else
    {
        //Get audio data
        int16_t* audio_data = (int16_t*)getMlvAudioData( m_pMlvObject );
        uint64_t audio_size = getMlvAudioSize( m_pMlvObject );
        //paint
        pic = QPixmap::fromImage( m_pAudioWave->getMonoWave( audio_data, audio_size, ui->labelAudioTrack->width(), devicePixelRatio() ) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        ui->labelAudioTrack->setPixmap( pic );
    }
    ui->labelAudioTrack->setMinimumSize( 1, 1 ); //Otherwise window won't be smaller than picture
    ui->labelAudioTrack->setAlignment( Qt::AlignCenter ); //Always in the middle
    ui->labelAudioTrack->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
    ui->labelAudioTrack->setMinimumHeight( 32 );
    ui->labelAudioTrack->setMaximumHeight( 32 );
}

//Draw Zebras, return: 1=under, 2=over, 3=under+over, 0=okay
uint8_t MainWindow::drawZebras()
{
    uint8_t underOver = 0;

    //If option not checked we do nothing
    if( !ui->actionShowZebras->isChecked() ) return underOver;

    //Get image
    QImage image = m_pGraphicsItem->pixmap().toImage();
    //Each pixel
    for( int y = 0; y < image.height(); y++ )
    {
        for( int x = 0; x < image.width(); x++ )
        {
            QColor pixel = image.pixelColor( x, y );
            //Overexposed
            if( pixel.lightness() >= 252 )
            {
                //Set color red
                image.setPixelColor( x, y, Qt::red );
                underOver |= 0x02;
            }
            //Underexposed
            if( pixel.lightness() <= 3 )
            {
                //Set color blue
                image.setPixelColor( x, y, Qt::blue );
                underOver |= 0x01;
            }
        }
    }
    //Set image with zebras to viewer
    m_pGraphicsItem->setPixmap( QPixmap::fromImage( image ) );

    return underOver;
}

//Write the frame number into the label
void MainWindow::drawFrameNumberLabel( void )
{
    if( m_fileLoaded )
    {
        m_pFrameNumber->setText( tr( "Frame %1/%2" )
                                 .arg( ui->horizontalSliderPosition->value() + 1 )
                                 .arg( ui->horizontalSliderPosition->maximum() + 1 ) );
    }
    else
    {
        m_pFrameNumber->setText( tr( "Frame 0/0" ) );
    }
}

//Set Toolbuttons Focus Pixels
void MainWindow::setToolButtonFocusPixels(int index)
{
    bool actualize = false;
    if( index == toolButtonFocusPixelsCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonFocusDotsOff->setChecked( true );
        break;
    case 1: ui->toolButtonFocusDotsOn->setChecked( true );
        break;
    case 2: ui->toolButtonFocusDotsCropRec->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonFocusPixelsChanged();
}

//Set Toolbuttons Focus Pixels Interpolation
void MainWindow::setToolButtonFocusPixelsIntMethod(int index)
{
    bool actualize = false;
    if( index == toolButtonFocusPixelsIntMethodCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonFocusDotMethod1->setChecked( true );
        break;
    case 1: ui->toolButtonFocusDotMethod2->setChecked( true );
        break;
    case 2: ui->toolButtonFocusDotMethod3->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonFocusPixelsIntMethodChanged();
}

//Set Toolbuttons Bad Pixels
void MainWindow::setToolButtonBadPixels(int index)
{
    bool actualize = false;
    if( index == toolButtonBadPixelsCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonBadPixelsOff->setChecked( true );
        break;
    case 1: ui->toolButtonBadPixelsOn->setChecked( true );
        break;
    case 2: ui->toolButtonBadPixelsForce->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonBadPixelsChanged();
}

//Set Toolbuttons Bad Pixels Search Method
void MainWindow::setToolButtonBadPixelsSearchMethod(int index)
{
    bool actualize = false;
    if( index == toolButtonBadPixelsSearchMethodCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonBadPixelsSearchMethodNormal->setChecked( true );
        break;
    case 1: ui->toolButtonBadPixelsSearchMethodAggressive->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonBadPixelsSearchMethodChanged();
}

//Set Toolbuttons Bad Pixels Interpolation
void MainWindow::setToolButtonBadPixelsIntMethod(int index)
{
    bool actualize = false;
    if( index == toolButtonBadPixelsIntMethodCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonBadPixelsMethod1->setChecked( true );
        break;
    case 1: ui->toolButtonBadPixelsMethod2->setChecked( true );
        break;
    case 2: ui->toolButtonBadPixelsMethod3->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonBadPixelsIntMethodChanged();
}

//Set Toolbuttons Chroma Smooth
void MainWindow::setToolButtonChromaSmooth(int index)
{
    bool actualize = false;
    if( index == toolButtonChromaSmoothCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonChromaOff->setChecked( true );
        break;
    case 1: ui->toolButtonChroma2x2->setChecked( true );
        break;
    case 2: ui->toolButtonChroma3x3->setChecked( true );
        break;
    case 3: ui->toolButtonChroma5x5->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonChromaSmoothChanged();
}

//Set Toolbuttons Pattern Noise
void MainWindow::setToolButtonPatternNoise(int index)
{
    bool actualize = false;
    if( index == toolButtonPatternNoiseCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonPatternNoiseOff->setChecked( true );
        break;
    case 1: ui->toolButtonPatternNoiseOn->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonPatternNoiseChanged();
}

//Set Toolbuttons Upside Down
void MainWindow::setToolButtonUpsideDown(int index)
{
    bool actualize = false;
    if( index == toolButtonUpsideDownCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonUpsideDownOff->setChecked( true );
        break;
    case 1: ui->toolButtonUpsideDownOn->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonUpsideDownChanged();
}

//Set Toolbuttons Vertical Stripes
void MainWindow::setToolButtonVerticalStripes(int index)
{
    bool actualize = false;
    if( index == toolButtonVerticalStripesCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonVerticalStripesOff->setChecked( true );
        break;
    case 1: ui->toolButtonVerticalStripesNormal->setChecked( true );
        break;
    case 2: ui->toolButtonVerticalStripesForce->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonVerticalStripesChanged();
}

//Set Toolbuttons Dual Iso
void MainWindow::setToolButtonDualIso(int index)
{
    bool actualize = false;
    if( index == toolButtonDualIsoCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonDualIsoOff->setChecked( true );
        break;
    case 1: ui->toolButtonDualIsoOn->setChecked( true );
        break;
    case 2: ui->toolButtonDualIsoPreview->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonDualIsoChanged();
}

//Set Toolbuttons Dual Iso Interpolation
void MainWindow::setToolButtonDualIsoInterpolation(int index)
{
    bool actualize = false;
    if( index == toolButtonDualIsoInterpolationCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonDualIsoInterpolationAmaze->setChecked( true );
        break;
    case 1: ui->toolButtonDualIsoInterpolationMean->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonDualIsoInterpolationChanged();
}

//Set Toolbuttons Dual Iso Alias Map
void MainWindow::setToolButtonDualIsoAliasMap(int index)
{
    bool actualize = false;
    if( index == toolButtonDualIsoAliasMapCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonDualIsoAliasMapOff->setChecked( true );
        break;
    case 1: ui->toolButtonDualIsoAliasMapOn->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonDualIsoAliasMapChanged();
}

//Set Toolbuttons Dual Iso Fullres Blending
void MainWindow::setToolButtonDualIsoFullresBlending(int index)
{
    bool actualize = false;
    if( index == toolButtonDualIsoFullresBlendingCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonDualIsoFullresBlendingOff->setChecked( true );
        break;
    case 1: ui->toolButtonDualIsoFullresBlendingOn->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonDualIsoFullresBlendingChanged();
}

//Set Toolbuttons Darkframe Subtraction On/Off
void MainWindow::setToolButtonDarkFrameSubtraction(int index)
{
    //Switch Darkframe Subtraction to OFF if internal was selected and no internal data is available
    if( !llrpGetDarkFrameIntStatus( m_pMlvObject ) && index == 2 ) index = 0;

    bool actualize = false;
    if( index == toolButtonDarkFrameSubtractionCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonDarkFrameSubtractionOff->setChecked( true );
        break;
    case 1: ui->toolButtonDarkFrameSubtractionExt->setChecked( true );
        break;
    case 2: ui->toolButtonDarkFrameSubtractionInt->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonDarkFrameSubtractionChanged( true );
}

//Set Toolbuttons gradation curve
void MainWindow::setToolButtonGCurves(int index)
{
    bool actualize = false;
    if( index == toolButtonGCurvesCurrentIndex() ) actualize = true;

    switch( index )
    {
    case 0: ui->toolButtonGCurvesY->setChecked( true );
        break;
    case 1: ui->toolButtonGCurvesR->setChecked( true );
        break;
    case 2: ui->toolButtonGCurvesG->setChecked( true );
        break;
    case 3: ui->toolButtonGCurvesB->setChecked( true );
        break;
    default: break;
    }
    if( actualize ) toolButtonGCurvesChanged();
}

//Get toolbutton index of focus pixels
int MainWindow::toolButtonFocusPixelsCurrentIndex()
{
    if( ui->toolButtonFocusDotsOff->isChecked() ) return 0;
    else if( ui->toolButtonFocusDotsOn->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton index of focus pixels interpolation
int MainWindow::toolButtonFocusPixelsIntMethodCurrentIndex()
{
    if( ui->toolButtonFocusDotMethod1->isChecked() ) return 0;
    if( ui->toolButtonFocusDotMethod2->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton index of bad pixels
int MainWindow::toolButtonBadPixelsCurrentIndex()
{
    if( ui->toolButtonBadPixelsOff->isChecked() ) return 0;
    else if( ui->toolButtonBadPixelsOn->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton index of bad pixels search method
int MainWindow::toolButtonBadPixelsSearchMethodCurrentIndex()
{
    if( ui->toolButtonBadPixelsSearchMethodNormal->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of bad pixels interpolation
int MainWindow::toolButtonBadPixelsIntMethodCurrentIndex()
{
    if( ui->toolButtonBadPixelsMethod1->isChecked() ) return 0;
    if( ui->toolButtonBadPixelsMethod2->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton index of chroma smooth
int MainWindow::toolButtonChromaSmoothCurrentIndex()
{
    if( ui->toolButtonChromaOff->isChecked() ) return 0;
    else if( ui->toolButtonChroma2x2->isChecked() ) return 1;
    else if( ui->toolButtonChroma3x3->isChecked() ) return 2;
    else return 3;
}

//Get toolbutton index of pattern noise
int MainWindow::toolButtonPatternNoiseCurrentIndex()
{
    if( ui->toolButtonPatternNoiseOff->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of upside down
int MainWindow::toolButtonUpsideDownCurrentIndex()
{
    if( ui->toolButtonUpsideDownOff->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of vertical stripes
int MainWindow::toolButtonVerticalStripesCurrentIndex()
{
    if( ui->toolButtonVerticalStripesOff->isChecked() ) return 0;
    else if( ui->toolButtonVerticalStripesNormal->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton index of dual iso force
int MainWindow::toolButtonDualIsoForceCurrentIndex()
{
    if( ui->toolButtonDualIsoForce->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of dual Iso
int MainWindow::toolButtonDualIsoCurrentIndex()
{
    if( ui->toolButtonDualIsoOff->isChecked() ) return 0;
    else if( ui->toolButtonDualIsoOn->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton index of dual iso interpolation
int MainWindow::toolButtonDualIsoInterpolationCurrentIndex()
{
    if( ui->toolButtonDualIsoInterpolationAmaze->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of dual iso alias map
int MainWindow::toolButtonDualIsoAliasMapCurrentIndex()
{
    if( ui->toolButtonDualIsoAliasMapOff->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of dual iso fullres blending
int MainWindow::toolButtonDualIsoFullresBlendingCurrentIndex()
{
    if( ui->toolButtonDualIsoFullresBlendingOff->isChecked() ) return 0;
    else return 1;
}

//Get toolbutton index of Darkframe Subtraction On/Off
int MainWindow::toolButtonDarkFrameSubtractionCurrentIndex()
{
    if( ui->toolButtonDarkFrameSubtractionOff->isChecked() ) return 0;
    else if( ui->toolButtonDarkFrameSubtractionExt->isChecked() ) return 1;
    else return 2;
}

//Get toolbutton inedx of gradation curves
int MainWindow::toolButtonGCurvesCurrentIndex()
{
    if( ui->toolButtonGCurvesY->isChecked() ) return 0;
    else if( ui->toolButtonGCurvesR->isChecked() ) return 1;
    else if( ui->toolButtonGCurvesG->isChecked() ) return 2;
    else return 3;
}

//About Window
void MainWindow::on_actionAbout_triggered()
{
    QPixmap pixmap = QPixmap( ":/IMG/IMG/Magic_Lantern_logo.png" )
                .scaled( 128 * devicePixelRatio(), 112 * devicePixelRatio(),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation );
        pixmap.setDevicePixelRatio( devicePixelRatio() );
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        pixmap.save(&buffer, "PNG");
        QString pic = QString("<img width='128' height='112' align='right' src=\"data:image/png;base64,") + byteArray.toBase64() + "\"/>";

        QMessageBox::about( this, QString( "About %1" ).arg( APPNAME ),
                                  QString(
                                    "<html>%1"
                                    "<body><h3>%2</h3>"
                                    " <p>%2 v%3</p>"
                                    " <p>%4</p>"
                                    " <p>See <a href='%5'>this site</a> for more information.</p>"
                                    " <p>Darkstyle Copyright (c) 2017, <a href='%6'>Juergen Skrotzky</a> under MIT</p>"
                                    " <p>Some icons by <a href='%7'>Double-J Design</a> under <a href='%8'>CC4.0</a></p>"
                                    " <p>Autoupdater Copyright (c) 2016, <a href='%9'>Violet Giraffe</a> under MIT</p>"
                                    " <p>Zhang-Wu LMMSE Image Demosaicking by Pascal Getreuer under <a href='%10'>BSD</a>.</p>"
                                    " <p>QRecentFilesMenu Copyright (c) 2011 by Morgan Leborgne under <a href='%11'>MIT</a>.</p>"
                                    " <p>Recursive bilateral filtering developed by Qingxiong Yang under <a href='%12'>MIT</a> and Ming under <a href='%13'>MIT</a>.</p>"
                                    " <p>AVIR image resizing algorithm designed by Aleksey Vaneev under <a href='%14'>MIT</a>.</p>"
                                    " <p>Sobel filter Copyright 2018 Pedro Melgueira under <a href='%15'>Apache 2.0</a>.</p>"
                                    " </body></html>" )
                                   .arg( pic ) //1
                                   .arg( APPNAME ) //2
                                   .arg( VERSION ) //3
                                   .arg( "by Ilia3101, bouncyball, Danne & masc." ) //4
                                   .arg( "https://github.com/ilia3101/MLV-App" ) //5
                                   .arg( "https://github.com/Jorgen-VikingGod" ) //6
                                   .arg( "http://www.doublejdesign.co.uk/" ) //7
                                   .arg( "https://creativecommons.org/licenses/by/4.0/" ) //8
                                   .arg( "https://github.com/VioletGiraffe/github-releases-autoupdater" ) //9
                                   .arg( "http://www.opensource.org/licenses/bsd-license.html" ) //10
                                   .arg( "https://github.com/mojocorp/QRecentFilesMenu/blob/master/LICENSE" ) //11
                                   .arg( "https://github.com/ufoym/recursive-bf/blob/master/LICENSE" ) //12
                                   .arg( "https://github.com/Fig1024/OP_RBF/blob/master/LICENSE" ) //13
                                   .arg( "https://github.com/avaneev/avir/blob/master/LICENSE" ) //14
                                   .arg( "https://github.com/petermlm/SobelFilter/blob/master/LICENSE" ) ); //15
}

//Qt Infobox
void MainWindow::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt( this );
}

//Position Slider
void MainWindow::on_horizontalSliderPosition_valueChanged(int position)
{
    //Enable jumping while drop frame mode playback is active
    if( ui->actionPlay->isChecked() && ui->actionDropFrameMode->isChecked() )
    {
        m_newPosDropMode = position;
        if( ui->actionAudioOutput->isChecked() )
        {
            m_tryToSyncAudio = true;
        }
    }

    m_frameChanged = true;
}

//Show Info Dialog
void MainWindow::on_actionClip_Information_triggered()
{
    if( !m_pInfoDialog->isVisible() ) m_pInfoDialog->show();
    else m_pInfoDialog->hide();
}

void MainWindow::on_horizontalSliderGamma_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetGamma( m_pProcessingObject, value );
    ui->label_GammaVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderExposure_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetExposureStops( m_pProcessingObject, value + 1.2 );
    ui->label_ExposureVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderExposureGradient_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetGradientExposure( m_pProcessingObject, value );
    ui->label_ExposureGradient->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderContrast_valueChanged(int position)
{
    processingSetSimpleContrast( m_pProcessingObject, position / 100.0 );
    ui->label_ContrastVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderContrastGradient_valueChanged(int position)
{
    processingSetSimpleContrastGradient( m_pProcessingObject, position / 100.0 );
    ui->label_ContrastGradientVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderTemperature_valueChanged(int position)
{
    int value = ( 218 - 42 ) * ( ui->horizontalSliderTemperature->value() - ui->horizontalSliderTemperature->minimum() ) / ( ui->horizontalSliderTemperature->maximum() - ui->horizontalSliderTemperature->minimum() );
    ui->horizontalSliderTemperature->setStyleSheet(
        QString( "QSlider::add-page:horizontal{background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(%1, 130, %2, 255), stop:1 rgba(218, 130, 42, 255));}"
                 "QSlider::sub-page:horizontal{background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(42, 130, 218, 255), stop:1 rgba(%1, 130, %2, 255));}"
                 "QSlider::add-page:horizontal:disabled{background:rgb(80,80,80);}"
                 "QSlider::sub-page:horizontal:disabled{background:rgb(80,80,80);}" ).arg( value+42 ).arg( 218-value )
    );

    ui->label_TemperatureVal->setText( QString("%1 K").arg( position ) );

    if( !m_fileLoaded ) return;
    processingSetWhiteBalanceKelvin( m_pProcessingObject, position );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderTint_valueChanged(int position)
{
    int value = ( 218 - 42 ) * ( ui->horizontalSliderTint->value() - ui->horizontalSliderTint->minimum() ) / ( ui->horizontalSliderTint->maximum() - ui->horizontalSliderTint->minimum() );
    ui->horizontalSliderTint->setStyleSheet(
        QString( "QSlider::add-page:horizontal{background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(%1, %2, %1, 255), stop:1 rgba(218, 42, 218, 255));}"
                 "QSlider::sub-page:horizontal{background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(42, 218, 42, 255), stop:1 rgba(%1, %2, %1, 255));}"
                 "QSlider::add-page:horizontal:disabled{background:rgb(80,80,80);}"
                 "QSlider::sub-page:horizontal:disabled{background:rgb(80,80,80);}" ).arg( value+42 ).arg( 218-value )
    );

    ui->label_TintVal->setText( QString("%1").arg( position ) );

    if( !m_fileLoaded ) return;
    processingSetWhiteBalanceTint( m_pProcessingObject, position / 10.0 );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderClarity_valueChanged(int position)
{
    processingSetClarity( m_pProcessingObject, position / 100.0 );
    ui->label_ClarityVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderVibrance_valueChanged(int position)
{
    double value = pow( ( position + 100 ) / 200.0 * 2.0, log( 3.6 )/log( 2.0 ) );
    processingSetVibrance( m_pProcessingObject, value );
    ui->label_VibranceVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderSaturation_valueChanged(int position)
{
    double value = pow( ( position + 100 ) / 200.0 * 2.0, log( 3.6 )/log( 2.0 ) );
    processingSetSaturation( m_pProcessingObject, value );
    ui->label_SaturationVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDS_valueChanged(int position)
{
    processingSetDCFactor( m_pProcessingObject, position * FACTOR_DS / 100.0 );
    ui->label_DsVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDR_valueChanged(int position)
{
    processingSetDCRange( m_pProcessingObject, position / 100.0 );
    ui->label_DrVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLS_valueChanged(int position)
{
    processingSetLCFactor( m_pProcessingObject, position * FACTOR_LS / 100.0 );
    ui->label_LsVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLR_valueChanged(int position)
{
    processingSetLCRange( m_pProcessingObject, position / 100.0 );
    ui->label_LrVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLighten_valueChanged(int position)
{
    processingSetLightening( m_pProcessingObject, position * FACTOR_LIGHTEN / 100.0 );
    ui->label_LightenVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderShadows_valueChanged(int position)
{
    processingSetShadows( m_pProcessingObject, position * 1.5 / 100.0 );
    ui->label_ShadowsVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderHighlights_valueChanged(int position)
{
    processingSetHighlights( m_pProcessingObject, position * 1.5 / 100.0 );
    ui->label_HighlightsVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderSharpen_valueChanged(int position)
{
    processingSetSharpening( m_pProcessingObject, position / 100.0 );
    ui->label_Sharpen->setText( QString("%1").arg( position ) );
    m_frameChanged = true;

    bool enable = true;
    if( position == 0 ) enable = false;
    ui->label_ShMasking->setEnabled( enable );
    ui->label_ShMaskingText->setEnabled( enable );
    ui->horizontalSliderShMasking->setEnabled( enable );
}

void MainWindow::on_horizontalSliderShMasking_valueChanged(int position)
{
    processingSetSharpenMasking( m_pProcessingObject, position );
    ui->label_ShMasking->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderChromaBlur_valueChanged(int position)
{
    processingSetChromaBlurRadius( m_pProcessingObject, position );
    ui->label_ChromaBlur->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDenoiseStrength_valueChanged(int position)
{
    processingSetDenoiserStrength( m_pProcessingObject, position );
    ui->label_DenoiseStrength->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderRbfDenoiseLuma_valueChanged(int position)
{
    processingSetRbfDenoiserLuma( m_pProcessingObject, position );
    ui->label_RbfDenoiseLuma->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderRbfDenoiseChroma_valueChanged(int position)
{
    processingSetRbfDenoiserChroma( m_pProcessingObject, position );
    ui->label_RbfDenoiseChroma->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderRbfDenoiseRange_valueChanged(int position)
{
    processingSetRbfDenoiserRange( m_pProcessingObject, position );
    ui->label_RbfDenoiseRange->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderGrainStrength_valueChanged(int position)
{
    processingSetGrainStrength( m_pProcessingObject, position );
    ui->label_GrainStrength->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLutStrength_valueChanged(int position)
{
    processingSetLutStrength( m_pProcessingObject, position );
    ui->label_LutStrengthVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderFilterStrength_valueChanged(int position)
{
    filterObjectSetFilterStrength( m_pProcessingObject->filter, position / 100.0 );
    ui->label_FilterStrengthVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderVignetteStrength_valueChanged(int position)
{
    processingSetVignetteStrength( m_pProcessingObject, position * 1.27 );
    ui->label_VignetteStrengthVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderVignetteRadius_valueChanged(int position)
{
    processingSetVignetteMask( m_pProcessingObject,
                               getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject),
                               position / 100.0,
                               ui->horizontalSliderVignetteShape->value() / 100.0,
                               getHorizontalStretchFactor(false),
                               getVerticalStretchFactor(false) );
    ui->label_VignetteRadiusVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderVignetteShape_valueChanged(int position)
{
    processingSetVignetteMask( m_pProcessingObject,
                               getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject),
                               ui->horizontalSliderVignetteRadius->value() / 100.0,
                               position / 100.0,
                               getHorizontalStretchFactor(false),
                               getVerticalStretchFactor(false) );
    ui->label_VignetteShapeVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderCaRed_valueChanged(int position)
{
    setMlvCaCorrectionRed( m_pMlvObject, (position / 10.0) );
    ui->label_CaRedVal->setText( QString("%1").arg( position / 10.0, 0, 'f', 1 ) );

    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderCaBlue_valueChanged(int position)
{
    setMlvCaCorrectionBlue( m_pMlvObject, (position / 10.0) );
    ui->label_CaBlueVal->setText( QString("%1").arg( position / 10.0, 0, 'f', 1 ) );

    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderCaDesaturate_valueChanged(int position)
{
    processingSetCaDesaturate( m_pProcessingObject, position );
    ui->label_CaDesaturateVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderCaRadius_valueChanged(int position)
{
    processingSetCaRadius( m_pProcessingObject, position );
    ui->label_CaRadiusVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderRawWhite_valueChanged(int position)
{
    if( !m_fileLoaded ) return;
    if( getMlvBitdepth( m_pMlvObject ) == 0 ) return;
    if( getMlvBitdepth( m_pMlvObject ) > 16 ) return;

    ui->label_RawWhiteVal->setText( QString("%1").arg( position ) );

    if( !ui->checkBoxRawFixEnable->isChecked() )
    {
        position = getMlvOriginalWhiteLevel( m_pMlvObject );
    }
    else if( position <= ui->horizontalSliderRawBlack->value() + 1 )
    {
        position = ui->horizontalSliderRawBlack->value() + 1;
        ui->horizontalSliderRawWhite->setValue( position );
    }

    while( !m_pRenderThread->isIdle() ) QThread::msleep(1);

    /* Set mlv raw white level to the slider value */
    setMlvWhiteLevel( m_pMlvObject, position );
    /* Set processing white level with correction */
    processingSetWhiteLevel( m_pProcessingObject, position, getMlvBitdepth( m_pMlvObject ) );

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderRawBlack_valueChanged(int position)
{
    if( !m_fileLoaded ) return;
    if( getMlvBitdepth( m_pMlvObject ) == 0 ) return;
    if( getMlvBitdepth( m_pMlvObject ) > 16 ) return;

    ui->label_RawBlackVal->setText( QString("%1").arg( position ) );

    if( !ui->checkBoxRawFixEnable->isChecked() )
    {
        position = getMlvOriginalBlackLevel( m_pMlvObject );
    }
    else if( position >= ui->horizontalSliderRawWhite->value() - 1 )
    {
        position = ui->horizontalSliderRawWhite->value() - 1;
        ui->horizontalSliderRawBlack->setValue( position );
    }

    while( !m_pRenderThread->isIdle() ) QThread::msleep(1);

    /* Set mlv raw white level to the slider value */
    setMlvBlackLevel( m_pMlvObject, position );
    /* Set processing white level with correction */
    processingSetBlackLevel( m_pProcessingObject, position, getMlvBitdepth( m_pMlvObject ) );

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderTone_valueChanged(int position)
{
    QColor color;
    color.setHslF( position/255.0, 1.0, 0.5 );
    QPixmap pixmap( 48, 18 );
    pixmap.fill( color );
    ui->label_ToningColor->setPixmap( pixmap );
    processingSetToning( m_pProcessingObject,
                         color.red(), color.green(), color.blue(),
                         ui->horizontalSliderToningStrength->value() );
    ui->label_ToneVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderToningStrength_valueChanged(int position)
{
    QColor color;
    color.setHslF( ui->horizontalSliderTone->value() / 255.0, 1.0, 0.5 );
    processingSetToning( m_pProcessingObject,
                         color.red(), color.green(), color.blue(),
                         position );
    ui->label_ToningStrengthVal->setText( QString("%1").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderVidstabStepsize_valueChanged(int position)
{
    ui->label_VidstabStepsizeVal->setText( QString("%1").arg( position ) );
}

void MainWindow::on_horizontalSliderVidstabShakiness_valueChanged(int position)
{
    ui->label_VidstabShakinessVal->setText( QString("%1").arg( position ) );
}

void MainWindow::on_horizontalSliderVidstabAccuracy_valueChanged(int position)
{
    ui->label_VidstabAccuracyVal->setText( QString("%1").arg( position ) );
}

void MainWindow::on_horizontalSliderVidstabZoom_valueChanged(int position)
{
    ui->label_VidstabZoomVal->setText( QString("%1").arg( position ) );
}

void MainWindow::on_horizontalSliderVidstabSmoothing_valueChanged(int position)
{
    ui->label_VidstabSmoothingVal->setText( QString("%1").arg( position ) );
}

void MainWindow::on_horizontalSliderExposure_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderExposure->setValue( sliders->exposure() );
    delete sliders;
}

void MainWindow::on_horizontalSliderExposureGradient_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderExposureGradient->setValue( sliders->gradientExposure() );
    delete sliders;
}

void MainWindow::on_horizontalSliderContrast_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderContrast->setValue( sliders->contrast() );
    delete sliders;
}

void MainWindow::on_horizontalSliderContrastGradient_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderContrastGradient->setValue( sliders->gradientContrast() );
    delete sliders;
}

void MainWindow::on_horizontalSliderTemperature_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    setWhiteBalanceFromMlv( sliders );
    ui->horizontalSliderTemperature->setValue( sliders->temperature() );
    delete sliders;
}

void MainWindow::on_horizontalSliderTint_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderTint->setValue( sliders->tint() );
    delete sliders;
}

void MainWindow::on_horizontalSliderClarity_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderClarity->setValue( sliders->clarity() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVibrance_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVibrance->setValue( sliders->vibrance() );
    delete sliders;
}

void MainWindow::on_horizontalSliderSaturation_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderSaturation->setValue( sliders->saturation() );
    delete sliders;
}

void MainWindow::on_horizontalSliderDS_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderDS->setValue( sliders->ds() );
    delete sliders;
}

void MainWindow::on_horizontalSliderDR_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderDR->setValue( sliders->dr() );
    delete sliders;
}

void MainWindow::on_horizontalSliderLS_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderLS->setValue( sliders->ls() );
    delete sliders;
}

void MainWindow::on_horizontalSliderLR_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderLR->setValue( sliders->lr() );
    delete sliders;
}

void MainWindow::on_horizontalSliderLighten_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderLighten->setValue( sliders->lightening() );
    delete sliders;
}

void MainWindow::on_horizontalSliderShadows_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderShadows->setValue( sliders->shadows() );
    delete sliders;
}

void MainWindow::on_horizontalSliderHighlights_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderHighlights->setValue( sliders->highlights() );
    delete sliders;
}

void MainWindow::on_horizontalSliderSharpen_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderSharpen->setValue( sliders->sharpen() );
    delete sliders;
}

void MainWindow::on_horizontalSliderShMasking_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderShMasking->setValue( sliders->shMasking() );
    delete sliders;
}

void MainWindow::on_horizontalSliderChromaBlur_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderChromaBlur->setValue( sliders->chromaBlur() );
    delete sliders;
}

void MainWindow::on_horizontalSliderDenoiseStrength_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderDenoiseStrength->setValue( sliders->denoiserStrength() );
    delete sliders;
}

void MainWindow::on_horizontalSliderRbfDenoiseLuma_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderRbfDenoiseLuma->setValue( sliders->rbfDenoiserLuma() );
    delete sliders;
}

void MainWindow::on_horizontalSliderRbfDenoiseChroma_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderRbfDenoiseChroma->setValue( sliders->rbfDenoiserChroma() );
    delete sliders;
}

void MainWindow::on_horizontalSliderRbfDenoiseRange_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderRbfDenoiseRange->setValue( sliders->rbfDenoiserRange() );
    delete sliders;
}

void MainWindow::on_horizontalSliderGrainStrength_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderGrainStrength->setValue( sliders->grainStrength() );
    delete sliders;
}

void MainWindow::on_horizontalSliderLutStrength_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderLutStrength->setValue( sliders->lutStrength() );
    delete sliders;
}

void MainWindow::on_horizontalSliderFilterStrength_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderFilterStrength->setValue( sliders->filterStrength() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVignetteStrength_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVignetteStrength->setValue( sliders->vignetteStrength() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVignetteRadius_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVignetteRadius->setValue( sliders->vignetteRadius() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVignetteShape_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVignetteShape->setValue( sliders->vignetteShape() );
    delete sliders;
}

void MainWindow::on_horizontalSliderCaRed_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderCaRed->setValue( sliders->caRed() );
    delete sliders;
}

void MainWindow::on_horizontalSliderCaBlue_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderCaBlue->setValue( sliders->caBlue() );
    delete sliders;
}

void MainWindow::on_horizontalSliderCaDesaturate_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderCaDesaturate->setValue( sliders->caDesaturate() );
    delete sliders;
}

void MainWindow::on_horizontalSliderCaRadius_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderCaRadius->setValue( sliders->caRadius() );
    delete sliders;
}

void MainWindow::on_horizontalSliderRawWhite_doubleClicked()
{
    ui->horizontalSliderRawWhite->setValue( getMlvOriginalWhiteLevel( m_pMlvObject ) );
}

void MainWindow::on_horizontalSliderRawBlack_doubleClicked()
{
    ui->horizontalSliderRawBlack->setValue( getMlvOriginalBlackLevel( m_pMlvObject ) );
}

void MainWindow::on_horizontalSliderTone_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderTone->setValue( sliders->tone() );
    delete sliders;
}

void MainWindow::on_horizontalSliderToningStrength_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderToningStrength->setValue( sliders->toningStrength() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVidstabStepsize_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVidstabStepsize->setValue( sliders->vidStabStepsize() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVidstabShakiness_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVidstabShakiness->setValue( sliders->vidStabShakiness() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVidstabAccuracy_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVidstabAccuracy->setValue( sliders->vidStabAccuracy() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVidstabZoom_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVidstabZoom->setValue( sliders->vidStabZoom() );
    delete sliders;
}

void MainWindow::on_horizontalSliderVidstabSmoothing_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderVidstabSmoothing->setValue( sliders->vidStabSmoothing() );
    delete sliders;
}

//Jump to first frame
void MainWindow::on_actionGoto_First_Frame_triggered()
{
    //If actual position is cut in, we jump to 0
    if( ui->horizontalSliderPosition->value() == ui->spinBoxCutIn->value() - 1 )
    {
        ui->horizontalSliderPosition->setValue( 0 );
        m_newPosDropMode = 0;
    }
    //Else we jump to cut in
    else
    {
        ui->horizontalSliderPosition->setValue( ui->spinBoxCutIn->value() - 1 );
        m_newPosDropMode = ui->spinBoxCutIn->value() - 1;
    }

    //Sync audio if playback and audio active
    if( ui->actionAudioOutput->isChecked()
     && ui->actionDropFrameMode->isChecked()
     && ui->actionPlay->isChecked() )
    {
        m_tryToSyncAudio = true;
    }
}

//Export clip
void MainWindow::on_actionExport_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //Save slider receipt
    setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );

    //Save last active clip before export
    m_lastClipBeforeExport = m_lastActiveClipInSession;

    //Filename proposal in dependency to actual file
    QString saveFileName = m_pSessionReceipts.at( m_lastActiveClipInSession )->fileName();
    //But take the folder from last export
    saveFileName = QString( "%1/%2" ).arg( m_lastExportPath ).arg( QFileInfo( saveFileName ).fileName() );

    QString fileType;
    QString fileEnding;
    saveFileName = saveFileName.left( saveFileName.lastIndexOf( "." ) );
    if( m_codecProfile == CODEC_AVI
     || m_codecProfile == CODEC_MJPEG
     || m_codecProfile == CODEC_FFVHUFF )
    {
        saveFileName.append( ".avi" );
        fileType = tr("Audio Video Interleave (*.avi)");
        fileEnding = ".avi";
    }
    else if( m_codecProfile == CODEC_CDNG
          || m_codecProfile == CODEC_CDNG_LOSSLESS
          || m_codecProfile == CODEC_CDNG_FAST )
    {
        saveFileName.append( ".dng" );
        fileType = tr("Cinema DNG (*.dng)");
        fileEnding = ".dng";
    }
    else if( m_codecProfile == CODEC_MLV )
    {
        saveFileName.append( ".MLV" );
        fileType = tr("Magic Lantern Video (*.MLV)");
        fileEnding = ".MLV";
    }
    else if( m_codecProfile == CODEC_TIFF )
    {
        saveFileName.append( ".tif" );
        fileType = tr("TIFF (*.tif)");
        fileEnding = ".tif";
    }
    else if( m_codecProfile == CODEC_JPG2K )
    {
        saveFileName.append( ".jp2" );
        fileType = tr("JPEG2000 (*.jp2)");
        fileEnding = ".jp2";
    }
    else if( m_codecProfile == CODEC_AUDIO_ONLY )
    {
        saveFileName.append( ".wav" );
        fileType = tr("Audio Wave (*.wav)");
        fileEnding = ".wav";
    }
    else
    {
        if( ( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265 )
         && ( m_codecOption == CODEC_H264_H_MP4 || m_codecOption == CODEC_H265_H_MP4
           || m_codecOption == CODEC_H264_M_MP4 || m_codecOption == CODEC_H265_M_MP4 ) )
        {
            saveFileName.append( ".mp4" );
            fileType = tr("MPEG-4 (*.mp4)");
            fileEnding = ".mp4";
        }
        else if( ( m_codecProfile == CODEC_H264 || m_codecProfile == CODEC_H265 )
         && ( m_codecOption == CODEC_H264_H_MKV || m_codecOption == CODEC_H265_H_MKV
           || m_codecOption == CODEC_H264_M_MKV || m_codecOption == CODEC_H265_M_MKV) )
        {
            saveFileName.append( ".mkv" );
            fileType = tr("Matroska (*.mkv)");
            fileEnding = ".mkv";
        }
        else
        {
            saveFileName.append( ".mov" );
            fileType = tr("Movie (*.mov)");
            fileEnding = ".mov";
        }
    }

    //If one file is selected, but not CDNG
    if( ( ui->listWidgetSession->selectedItems().count() <= 1 )
     && ( ( m_codecProfile != CODEC_CDNG ) && ( m_codecProfile != CODEC_CDNG_LOSSLESS ) && ( m_codecProfile != CODEC_CDNG_FAST ) ) )
    {
        //File Dialog
        QString fileName = QFileDialog::getSaveFileName( this, tr("Export..."),
                                                        saveFileName,
                                                        fileType );

        //Exit if not an MOV/AVI file or aborted
        if( fileName == QString( "" )
                && !fileName.endsWith( fileEnding, Qt::CaseInsensitive ) ) return;

        //Save last path for next time
        m_lastExportPath = QFileInfo( fileName ).absolutePath();

        //Get receipt into queue
        addClipToExportQueue( m_lastActiveClipInSession, fileName );
    }
    //if multiple files selected
    else
    {
        //Folder Dialog
        QString folderName = QFileDialog::getExistingDirectory(this, tr("Choose Export Folder"),
                                                          QFileInfo( saveFileName ).absolutePath(),
                                                          QFileDialog::ShowDirsOnly
                                                          | QFileDialog::DontResolveSymlinks);

        if( folderName.length() == 0 ) return;

        QStringList overwriteList;
        for( int row = 0; row < ui->listWidgetSession->count(); row++ )
        {
            if( !ui->listWidgetSession->item( row )->isSelected() ) continue;
            QString fileName = ui->listWidgetSession->item( row )->text().replace( ".mlv", fileEnding, Qt::CaseInsensitive );
            fileName.prepend( "/" );
            fileName.prepend( folderName );

            if( QFileInfo( fileName ).exists() ) overwriteList.append( fileName );
        }
        if( !overwriteList.empty() )
        {
            //qDebug() << "Files will be overwritten:" << overwriteList;
            OverwriteListDialog *listDialog = new OverwriteListDialog( this );
            listDialog->ui->listWidget->addItems( overwriteList );
            if( !listDialog->exec() )
            {
                delete listDialog;
                return;
            }
            delete listDialog;
        }

        //Save last path for next time
        m_lastExportPath = folderName;

        //for all selected
        for( int row = 0; row < ui->listWidgetSession->count(); row++ )
        {
            if( !ui->listWidgetSession->item( row )->isSelected() ) continue;

            //Create Path+Name
            QString fileName = ui->listWidgetSession->item( row )->text().replace( ".mlv", fileEnding, Qt::CaseInsensitive );
            fileName.prepend( "/" );
            fileName.prepend( folderName );

            //Get receipt into queue
            addClipToExportQueue( row, fileName );
        }
    }
    //Block GUI
    setEnabled( false );
    m_pStatusDialog->setEnabled( true );

    //Scripting class wants to know the export folder
    m_pScripting->setExportDir( QFileInfo( m_exportQueue.first()->exportFileName() ).absolutePath() );
    QStringList fileNames;
    for( int i = 0; i < m_exportQueue.count(); i++ )
    {
        fileNames.append( m_exportQueue.at( i )->fileName() );
    }
    m_pScripting->setMlvFileNames( fileNames );

    //startExport
    exportHandler();
}

//Export actual frame as 16bit png
void MainWindow::on_actionExportCurrentFrame_triggered()
{
    SingleFrameExportDialog *exportDialog = new SingleFrameExportDialog( this,
                                                                         m_pMlvObject,
                                                                         m_pSessionReceipts.at( m_lastActiveClipInSession )->fileName(),
                                                                         ui->horizontalSliderPosition->value(),
                                                                         getHorizontalStretchFactor(true),
                                                                         getVerticalStretchFactor(true) );
    exportDialog->exec();
    delete exportDialog;
}

//Enable / Disable the highlight reconstruction
void MainWindow::on_checkBoxHighLightReconstruction_toggled(bool checked)
{
    if( checked ) processingEnableHighlightReconstruction( m_pProcessingObject );
    else processingDisableHighlightReconstruction( m_pProcessingObject );
    m_frameChanged = true;
}

//Enable / Disable the camera matrix calculation
void MainWindow::on_comboBoxUseCameraMatrix_currentIndexChanged(int index)
{
    switch( index )
    {
        case 0: processingDontUseCamMatrix( m_pProcessingObject );
            break;
        case 1: processingUseCamMatrix( m_pProcessingObject );
            break;
        case 2: processingUseCamMatrixDanne( m_pProcessingObject );
            break;
        default: break;
    }

    if( index != 0 && !m_inOpeningProcess ) on_horizontalSliderTemperature_valueChanged( ui->horizontalSliderTemperature->value() );

    ui->label_Gamut->setEnabled( (bool)index );
    ui->comboBoxProcessingGamut->setEnabled( (bool)index );

    m_frameChanged = true;
}

//Enable / Disable the creative adjustments (all sliders and curves if log profile selected)
void MainWindow::on_checkBoxCreativeAdjustments_toggled(bool checked)
{
    if( checked )
    {
        //ui->checkBoxCreativeAdjustments->setIcon( QIcon( ":/RetinaIMG/RetinaIMG/Status-dialog-warning-icon.png" ) );
        processingAllowCreativeAdjustments( m_pProcessingObject );
    }
    else
    {
        //ui->checkBoxCreativeAdjustments->setIcon( QIcon() );
        processingDontAllowCreativeAdjustments( m_pProcessingObject );
    }
    if( ui->checkBoxCreativeAdjustments->isEnabled() ) enableCreativeAdjustments( checked );
    m_frameChanged = true;
}

//Enable / Disable chroma separation
void MainWindow::on_checkBoxChromaSeparation_toggled(bool checked)
{
    //Enable / Disable chroma blur
    ui->label_ChromaBlur->setEnabled( checked );
    ui->label_ChromaBlurText->setEnabled( checked );
    ui->horizontalSliderChromaBlur->setEnabled( checked );

    if( checked ) processingEnableChromaSeparation( m_pProcessingObject );
    else processingDisableChromaSeparation( m_pProcessingObject );
    m_frameChanged = true;
}

//Chose profile
void MainWindow::on_comboBoxProfile_currentIndexChanged(int index)
{
    if( index == 0 ) return;
    ui->comboBoxProfile->setCurrentIndex( 0 );
    index--;

    processingSetImageProfile(m_pProcessingObject, index);
    m_frameChanged = true;
    //Disable parameters if log
    ui->checkBoxCreativeAdjustments->blockSignals( true );
    ui->checkBoxCreativeAdjustments->setChecked( processingGetAllowedCreativeAdjustments( m_pProcessingObject ) );
    ui->checkBoxCreativeAdjustments->setEnabled( true );
    enableCreativeAdjustments( processingGetAllowedCreativeAdjustments( m_pProcessingObject ) );
    ui->checkBoxCreativeAdjustments->blockSignals( false );
    ui->comboBoxTonemapFct->blockSignals( true );
    ui->comboBoxTonemapFct->setCurrentIndex( processingGetTonemappingFunction( m_pProcessingObject ) );
    ui->comboBoxTonemapFct->blockSignals( false );
    ui->comboBoxProcessingGamut->blockSignals( true );
    ui->comboBoxProcessingGamut->setCurrentIndex( processingGetGamut( m_pProcessingObject ) );
    ui->comboBoxProcessingGamut->blockSignals( false );
    ui->horizontalSliderGamma->setValue( processingGetGamma( m_pProcessingObject ) * 100 );
}

//Chose profile, without changing the index
void MainWindow::on_comboBoxProfile_activated(int index)
{
    on_comboBoxProfile_currentIndexChanged( index );
}

//Choose Tonemapping Function
void MainWindow::on_comboBoxTonemapFct_currentIndexChanged(int index)
{
    processingSetTonemappingFunction( m_pProcessingObject, index );
    m_frameChanged = true;
}

//Choose Processing Gamut
void MainWindow::on_comboBoxProcessingGamut_currentIndexChanged(int index)
{
    processingSetGamut( m_pProcessingObject, index );
    m_frameChanged = true;
}

//Switch on/off all creative adjustment elements
void MainWindow::enableCreativeAdjustments( bool enable )
{
    ui->horizontalSliderLS->setEnabled( enable );
    ui->horizontalSliderLR->setEnabled( enable );
    ui->horizontalSliderDS->setEnabled( enable );
    ui->horizontalSliderDR->setEnabled( enable );
    ui->horizontalSliderLighten->setEnabled( enable );
    ui->horizontalSliderVibrance->setEnabled( enable );
    ui->horizontalSliderSaturation->setEnabled( enable );
    ui->horizontalSliderContrast->setEnabled( enable );
    ui->horizontalSliderClarity->setEnabled( enable );
    ui->horizontalSliderHighlights->setEnabled( enable );
    ui->horizontalSliderShadows->setEnabled( enable );
    ui->horizontalSliderContrastGradient->setEnabled( enable );
    ui->label_LsVal->setEnabled( enable );
    ui->label_LrVal->setEnabled( enable );
    ui->label_DsVal->setEnabled( enable );
    ui->label_DrVal->setEnabled( enable );
    ui->label_LightenVal->setEnabled( enable );
    ui->label_VibranceVal->setEnabled( enable );
    ui->label_SaturationVal->setEnabled( enable );
    ui->label_ContrastVal->setEnabled( enable );
    ui->label_ClarityVal->setEnabled( enable );
    ui->label_HighlightsVal->setEnabled( enable );
    ui->label_ShadowsVal->setEnabled( enable );
    ui->label_ContrastGradientVal->setEnabled( enable );
    ui->label_ls->setEnabled( enable );
    ui->label_lr->setEnabled( enable );
    ui->label_ds->setEnabled( enable );
    ui->label_dr->setEnabled( enable );
    ui->label_lighten->setEnabled( enable );
    ui->label_vibrance->setEnabled( enable );
    ui->label_saturation->setEnabled( enable );
    ui->label_contrast->setEnabled( enable );
    ui->label_clarity->setEnabled( enable );
    ui->label_highlights->setEnabled( enable );
    ui->label_shadows->setEnabled( enable );
    ui->label_contrast_gradient->setEnabled( enable );
    ui->groupBoxHsl->setEnabled( enable );
    ui->groupBoxToning->setEnabled( enable );
    ui->label_gradationcurves->setEnabled( enable );
    ui->toolButtonGCurvesY->setEnabled( enable );
    ui->toolButtonGCurvesR->setEnabled( enable );
    ui->toolButtonGCurvesG->setEnabled( enable );
    ui->toolButtonGCurvesB->setEnabled( enable );
    ui->toolButtonGCurvesReset->setEnabled( enable );
    ui->toolButtonGCurvesResetOne->setEnabled( enable );
    ui->labelCurves->setEnabled( enable );
}

//Calcukate and show resulting resolution after stretching
void MainWindow::resultingResolution( void )
{
    if( !ui->listWidgetSession->count() ) return;
    int x = getMlvWidth( m_pMlvObject ) * getHorizontalStretchFactor( false );
    int y = getMlvHeight( m_pMlvObject ) * getVerticalStretchFactor( false );
    ui->label_resResolution->setText( QString( "%1 x %2 pixels" ).arg(x).arg(y) );
}

//Chose filter
void MainWindow::on_comboBoxFilterName_currentIndexChanged(int index)
{
    filterObjectSetFilter( m_pProcessingObject->filter, index );
    m_frameChanged = true;
}

//Denoiser Window Selection
void MainWindow::on_comboBoxDenoiseWindow_currentIndexChanged(int index)
{
    processingSetDenoiserWindow( m_pProcessingObject, index + 2 );
    m_frameChanged = true;
}

//Click on Zoom: fit
void MainWindow::on_actionZoomFit_triggered(bool on)
{
    if( !on )
    {
        ui->actionZoomFit->setChecked( false );
        on_actionZoom100_triggered();
    }
    else
    {
        ui->graphicsView->resetZoom();
        ui->graphicsView->setZoomEnabled( false );
        ui->actionZoomFit->setChecked( true );
        m_frameChanged = true;
    }
    m_zoomModeChanged = true;
}

//Click on Zoom: 100%
void MainWindow::on_actionZoom100_triggered()
{
    ui->actionZoomFit->setChecked( false );
    if( !m_fileLoaded )
    {
        return;
    }
    ui->graphicsView->resetZoom();
    if( !ui->graphicsView->isZoomEnabled() )
    {
        ui->graphicsView->setZoomEnabled( true );
        m_zoomTo100Center = true;
    }
    m_frameChanged = true;
}

//Show Histogram
void MainWindow::on_actionShowHistogram_triggered(void)
{
    m_frameChanged = true;
}

//Show Waveform
void MainWindow::on_actionShowWaveFormMonitor_triggered(void)
{
    m_frameChanged = true;
}

//Show Parade
void MainWindow::on_actionShowParade_triggered()
{
    m_frameChanged = true;
}

//Show VectorScope
void MainWindow::on_actionShowVectorScope_triggered()
{
    m_frameChanged = true;
}

//Use none debayer (speedy B&W)
void MainWindow::on_actionUseNoneDebayer_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Use simple debayer (speedy colored)
void MainWindow::on_actionUseSimpleDebayer_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Don't use AMaZE -> bilinear
void MainWindow::on_actionUseBilinear_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Use LMMSE debayer
void MainWindow::on_actionUseLmmseDebayer_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Use IGV debayer
void MainWindow::on_actionUseIgvDebayer_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Use AHD debayer
void MainWindow::on_actionUseAhdDebayer_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Use AMaZE or not
void MainWindow::on_actionAlwaysUseAMaZE_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//En-/Disable Caching
void MainWindow::on_actionCaching_triggered()
{
    /* Use AMaZE */
    setMlvAlwaysUseAmaze( m_pMlvObject );

    enableMlvCaching( m_pMlvObject );

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_frameChanged = true;
}

//Use same debayer for playback like in edit panel
void MainWindow::on_actionDontSwitchDebayerForPlayback_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Select the codec
void MainWindow::on_actionExportSettings_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    ExportSettingsDialog *pExportSettings = new ExportSettingsDialog( this,
                                                                      m_pScripting,
                                                                      m_codecProfile,
                                                                      m_codecOption,
                                                                      m_exportDebayerMode,
                                                                      m_resizeFilterEnabled,
                                                                      m_resizeWidth,
                                                                      m_resizeHeight,
                                                                      m_fpsOverride,
                                                                      m_frameRate,
                                                                      m_audioExportEnabled,
                                                                      m_resizeFilterHeightLocked,
                                                                      m_smoothFilterSetting,
                                                                      m_hdrExport );
    pExportSettings->exec();
    m_codecProfile = pExportSettings->encoderSetting();
    m_codecOption = pExportSettings->encoderOption();
    m_exportDebayerMode = pExportSettings->debayerMode();
    m_resizeFilterEnabled = pExportSettings->isResizeEnabled();
    m_resizeWidth = pExportSettings->resizeWidth();
    m_resizeHeight = pExportSettings->resizeHeight();
    m_fpsOverride = pExportSettings->isFpsOverride();
    m_frameRate = pExportSettings->getFps();
    m_audioExportEnabled = pExportSettings->isExportAudioEnabled();
    m_resizeFilterHeightLocked = pExportSettings->isHeightLocked();
    m_smoothFilterSetting = pExportSettings->smoothSetting();
    m_hdrExport = pExportSettings->hdrBlending();
    delete pExportSettings;

    if( m_fileLoaded )
    {
        //Restart timer with chosen framerate
        killTimer( m_timerId );
        m_timerId = startTimer( (int)( 1000.0 / getFramerate() ) );

        //Refresh Timecode Label
        if( m_tcModeDuration )
        {
            QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                              30 * devicePixelRatio(),
                                                                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
            pic.setDevicePixelRatio( devicePixelRatio() );
            m_pTcLabel->setPixmap( pic );
        }
        else
        {
            QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->horizontalSliderPosition->value(), getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                              30 * devicePixelRatio(),
                                                                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
            pic.setDevicePixelRatio( devicePixelRatio() );
            m_pTcLabel->setPixmap( pic );
        }

    }

}

//Reset the edit sliders to default
void MainWindow::on_actionResetReceipt_triggered()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    sliders->setRawWhite( getMlvOriginalWhiteLevel( m_pMlvObject ) );
    sliders->setRawBlack( getMlvOriginalBlackLevel( m_pMlvObject ) );
    setSliders( sliders, false );
    delete sliders;
}

//Copy receipt to clipboard
void MainWindow::on_actionCopyRecept_triggered()
{
    m_pCopyMask->exec();

    setReceipt( m_pReceiptClipboard );
    ui->actionPasteReceipt->setEnabled( true );
}

//Paste receipt from clipboard
void MainWindow::on_actionPasteReceipt_triggered()
{
    for( int row = 0; row < ui->listWidgetSession->count(); row++ )
    {
        if( !ui->listWidgetSession->item( row )->isSelected() ) continue;
        //Save current settings into receipt
        if( row == m_lastActiveClipInSession )
        {
            setReceipt( m_pSessionReceipts.at(row) );
        }
        //Each selected clip gets the copied receipt
        replaceReceipt( m_pSessionReceipts.at(row), m_pReceiptClipboard, true );
        //If the actual is selected (may have changed since copy action), set sliders and get receipt
        if( row == m_lastActiveClipInSession )
        {
            setSliders( m_pSessionReceipts.at(row), true );
        }
    }
}

//New Session
void MainWindow::on_actionNewSession_triggered()
{
    //Save last session?
    if( ui->listWidgetSession->count() != 0 )
    {
        int ret = QMessageBox::warning( this, APPNAME, tr( "Do you like to save the current session?" ),
                                        tr( "Cancel" ), tr( "Don't save" ), tr( "Save" ) );

        //Aborted
        if( ret == QMessageBox::Escape || ret == 0 )
        {
            return;
        }
        //Save and quit
        else if( ret == 2 )
        {
            on_actionSaveSession_triggered();
            //Saving was aborted -> abort quit
            if( m_sessionFileName.count() == 0 )
            {
                return;
            }
        }
    }

    deleteSession();
}

//Open Session
void MainWindow::on_actionOpenSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSessionFileName ).absolutePath();
    if( !QDir( path ).exists() ) path = QDir::homePath();

    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open MLV App Session Xml"), path,
                                           tr("MLV App Session Xml files (*.masxml)"));

    //Abort selected
    if( fileName.count() == 0 ) return;

    m_inOpeningProcess = true;
    openSession( fileName );
    //Show last imported file
    showFileInEditor( m_pSessionReceipts.count() - 1 );
    m_sessionFileName = fileName;
    m_lastSessionFileName = fileName;
    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Save Session (just save)
void MainWindow::on_actionSaveSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    if( m_sessionFileName.count() == 0 ) on_actionSaveAsSession_triggered();
    else saveSession( m_sessionFileName );
}

//Save Session with filename selection
void MainWindow::on_actionSaveAsSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSessionFileName ).absolutePath();
    QString fileName = QFileDialog::getSaveFileName(this,
                                           tr("Save MLV App Session Xml"), path,
                                           tr("MLV App Session Xml files (*.masxml)"));

    //Abort selected
    if( fileName.count() == 0 ) return;

    //Add ending, if it got lost using some OS...
    if( !fileName.endsWith( ".masxml" ) ) fileName.append( ".masxml" );

    m_sessionFileName = fileName;
    m_lastSessionFileName = fileName;

    saveSession( fileName );
}

//Jump to next clip
void MainWindow::on_actionNext_Clip_triggered()
{
    if( ( ( m_lastActiveClipInSession + 1 ) < ui->listWidgetSession->count() ) && m_fileLoaded )
    {
        ui->listWidgetSession->setCurrentRow( m_lastActiveClipInSession + 1 );
        showFileInEditor( m_lastActiveClipInSession + 1 );
    }
}

//Jump to previous clip
void MainWindow::on_actionPrevious_Clip_triggered()
{
    if( ( m_lastActiveClipInSession > 0 ) && m_fileLoaded )
    {
        ui->listWidgetSession->setCurrentRow( m_lastActiveClipInSession - 1 );
        showFileInEditor( m_lastActiveClipInSession - 1 );
    }
}

//Select all clips via action
void MainWindow::on_actionSelectAllClips_triggered()
{
    if( ui->listWidgetSession->count() > 0 )
    {
        selectAllFiles();
    }
}

//Delete clip from session via action
void MainWindow::on_actionDeleteSelectedClips_triggered()
{
    if( ui->listWidgetSession->count() > 0 )
    {
        deleteFileFromSession();
        ui->actionDeleteSelectedClips->setEnabled( false );
    }
}

//FileName in SessionList doubleClicked
void MainWindow::on_listWidgetSession_activated(const QModelIndex &index)
{
    showFileInEditor( index.row() );
}

//Sessionlist visibility changed -> redraw picture
void MainWindow::on_dockWidgetSession_visibilityChanged(bool visible)
{
    if( !isMinimized() )
    {
        ui->actionShowSessionArea->setChecked( visible );
        qApp->processEvents();
        m_frameChanged = true;
    }
}

//Edit area visibility changed -> redraw picture
void MainWindow::on_dockWidgetEdit_visibilityChanged(bool visible)
{
    if( !isMinimized() )
    {
        ui->actionShowEditArea->setChecked( visible );
        qApp->processEvents();
        m_frameChanged = true;
    }
}

//Set visibility of audio track
void MainWindow::on_actionShowAudioTrack_toggled(bool checked)
{
    ui->labelAudioTrack->setVisible( checked );
    qApp->processEvents();
    m_frameChanged = true;
}

//Rightclick on SessionList
void MainWindow::on_listWidgetSession_customContextMenuRequested(const QPoint &pos)
{
    //if( ui->listWidgetSession->count() <= 0 ) return;
    //if( ui->listWidgetSession->selectedItems().size() <= 0 ) return;

    // Handle global position
    QPoint globalPos = ui->listWidgetSession->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    if( ui->listWidgetSession->count() > 0 )
    {
        if( ui->listWidgetSession->selectedItems().size() == 1 )
        {
            myMenu.addAction( ui->actionSelectAllClips );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Image-icon.png" ), "Show in Editor",  this, SLOT( rightClickShowFile() ) );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Delete-icon.png" ), "Delete Selected File from Session",  this, SLOT( deleteFileFromSession() ) );
            myMenu.addSeparator();
            myMenu.addAction( ui->actionShowInFinder );
            myMenu.addAction( ui->actionOpenWithExternalApplication );
            myMenu.addAction( ui->actionSelectExternalApplication );
            myMenu.addSeparator();
        }
        else if( ui->listWidgetSession->selectedItems().size() > 1 )
        {
            myMenu.addAction( ui->actionPasteReceipt );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Delete-icon.png" ), "Delete Selected Files from Session",  this, SLOT( deleteFileFromSession() ) );
            myMenu.addSeparator();
        }
    }
    myMenu.addMenu( ui->menuSessionListPreview );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Delete selected files from session
void MainWindow::deleteFileFromSession( void )
{
    //Save slider receipt
    setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );

    //Ask for options
    int delFile = QMessageBox::question( this, tr( "%1 - Remove clip" ).arg( APPNAME ), tr( "Remove clip from session, or delete clip from disk?" ), tr( "Remove" ), tr( "Delete from Disk" ), tr( "Abort" ) );
    if( delFile == 2 ) return; //Abort

    //If multiple selection is on, we need to erase all selected items
    for( int i = ui->listWidgetSession->selectedItems().size(); i > 0; i-- )
    {
        int row = ui->listWidgetSession->row( ui->listWidgetSession->selectedItems().at( i - 1 ) );
        //Delete file from disk when wanted
        if( delFile == 1 )
        {
            //MLV
#ifdef Q_OS_WIN //On windows the file has to be closed before beeing able to move to trash
            m_fileLoaded = false;
            m_dontDraw = true;
            freeMlvObject( m_pMlvObject );
            m_pMlvObject = initMlvObject();
#endif
            if( MoveToTrash( m_pSessionReceipts.at(row)->fileName() ) ) QMessageBox::critical( this, tr( "%1 - Delete clip from disk" ).arg( APPNAME ), tr( "Delete clip failed!" ) );
            //MAPP
            QString mappName = m_pSessionReceipts.at(row)->fileName();
            mappName.chop( 4 );
            mappName.append( ".MAPP" );
            if( QFileInfo( mappName ).exists() )
            {
                if( MoveToTrash( mappName ) ) QMessageBox::critical( this, tr( "%1 - Delete MAPP file from disk" ).arg( APPNAME ), tr( "Delete MAPP file failed!" ) );
            }
        }
        //Remove item from Session List
        delete ui->listWidgetSession->selectedItems().at( i - 1 );
        //Remove slider memory
        m_pSessionReceipts.removeAt( row );
        //influences actual loaded clip?
        if( m_lastActiveClipInSession > row ) m_lastActiveClipInSession--;
        if( m_lastActiveClipInSession < 0 ) m_lastActiveClipInSession = 0;
    }
    //if there is at least one...
    if( ui->listWidgetSession->count() > 0 )
    {
        //Open the nearest clip from last opened!
        if( m_lastActiveClipInSession >= ui->listWidgetSession->count() ) m_lastActiveClipInSession = ui->listWidgetSession->count() - 1;
        ui->listWidgetSession->setCurrentRow( m_lastActiveClipInSession );
        openMlv( ui->listWidgetSession->item( m_lastActiveClipInSession )->toolTip() );
        setSliders( m_pSessionReceipts.at( m_lastActiveClipInSession ), false );

        //Caching is in which state? Set it!
        if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
    }
    else
    {
        //All black
        deleteSession();
    }
}

//Shows the file, which is selected via contextmenu
void MainWindow::rightClickShowFile( void )
{
    showFileInEditor( ui->listWidgetSession->currentRow() );
}

//Select all files in SessionList
void MainWindow::selectAllFiles( void )
{
    ui->listWidgetSession->selectAll();
}

//Contextmenu on picture
void MainWindow::pictureCustomContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->graphicsView->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionZoomFit );
    myMenu.addAction( ui->actionZoom100 );
    myMenu.addSeparator();
    myMenu.addMenu( ui->menuDemosaicForPlayback );
    myMenu.addAction( ui->actionBetterResizer );
    myMenu.addSeparator();
    myMenu.addAction( ui->actionShowZebras );
    if( ui->actionFullscreen->isChecked() )
    {
        myMenu.addSeparator();
        myMenu.addAction( ui->actionGoto_First_Frame );
        myMenu.addAction( ui->actionPreviousFrame );
        myMenu.addAction( ui->actionPlay );
        myMenu.addAction( ui->actionNextFrame );
        myMenu.addAction( ui->actionLoop );
        myMenu.addSeparator();
        myMenu.addAction( ui->actionFullscreen );
    }
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Contextmenu on scope
void MainWindow::on_labelScope_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->labelScope->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionShowHistogram );
    myMenu.addAction( ui->actionShowWaveFormMonitor );
    myMenu.addAction( ui->actionShowParade );
    myMenu.addAction( ui->actionShowVectorScope );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//DoubleClick on Gamma Label
void MainWindow::on_label_GammaVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderGamma, ui->label_GammaVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderGamma->setValue( editSlider.getValue() );
}

//DoubleClick on Exposure Label
void MainWindow::on_label_ExposureVal_doubleClicked( void )
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderExposure, ui->label_ExposureVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderExposure->setValue( editSlider.getValue() );
}

//DoubleClick on Exposure Gradient Label
void MainWindow::on_label_ExposureGradient_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderExposureGradient, ui->label_ExposureGradient, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderExposureGradient->setValue( editSlider.getValue() );
}

//DoubleClick on Contrast Label
void MainWindow::on_label_ContrastVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderContrast, ui->label_ContrastVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderContrast->setValue( editSlider.getValue() );
}

//DoubleClick on Contrast Gradient Label
void MainWindow::on_label_ContrastGradientVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderContrastGradient, ui->label_ContrastGradientVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderContrastGradient->setValue( editSlider.getValue() );
}

//DoubleClick on Temperature Label
void MainWindow::on_label_TemperatureVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderTemperature, ui->label_TemperatureVal, 1.0, 0, 1.0 );
    editSlider.ui->doubleSpinBox->setValue( ui->label_TemperatureVal->text().left(5).toInt() );
    editSlider.ui->doubleSpinBox->selectAll();
    editSlider.exec();
    ui->horizontalSliderTemperature->setValue( editSlider.getValue() );
}

//DoubleClick on Tint Label
void MainWindow::on_label_TintVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderTint, ui->label_TintVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderTint->setValue( editSlider.getValue() );
}

//DoubleClick on Clarity Label
void MainWindow::on_label_ClarityVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderClarity, ui->label_ClarityVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderClarity->setValue( editSlider.getValue() );
}

//DoubleClick on Vibrance Label
void MainWindow::on_label_VibranceVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVibrance, ui->label_VibranceVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVibrance->setValue( editSlider.getValue() );
}

//DoubleClick on Saturation Label
void MainWindow::on_label_SaturationVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderSaturation, ui->label_SaturationVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderSaturation->setValue( editSlider.getValue() );
}

//DoubleClick on Dr Label
void MainWindow::on_label_DrVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDR, ui->label_DrVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderDR->setValue( editSlider.getValue() );
}

//DoubleClick on Ds Label
void MainWindow::on_label_DsVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDS, ui->label_DsVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderDS->setValue( editSlider.getValue() );
}

//DoubleClick on Lr Label
void MainWindow::on_label_LrVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLR, ui->label_LrVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderLR->setValue( editSlider.getValue() );
}

//DoubleClick on Ls Label
void MainWindow::on_label_LsVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLS, ui->label_LsVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderLS->setValue( editSlider.getValue() );
}

//DoubleClick on Lighten Label
void MainWindow::on_label_LightenVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLighten, ui->label_LightenVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderLighten->setValue( editSlider.getValue() );
}

//DoubleClick on Shadows Label
void MainWindow::on_label_ShadowsVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderShadows, ui->label_ShadowsVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderShadows->setValue( editSlider.getValue() );
}

//DoubleClick on Highlights Label
void MainWindow::on_label_HighlightsVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderHighlights, ui->label_HighlightsVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderHighlights->setValue( editSlider.getValue() );
}

//DoubleClick on Sharpen Label
void MainWindow::on_label_Sharpen_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderSharpen, ui->label_Sharpen, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderSharpen->setValue( editSlider.getValue() );
}

//DoubleClick on Sharpen Masking Label
void MainWindow::on_label_ShMasking_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderShMasking, ui->label_ShMasking, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderShMasking->setValue( editSlider.getValue() );
}

//DoubleClick on ChromaBlur Label
void MainWindow::on_label_ChromaBlur_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderChromaBlur, ui->label_ChromaBlur, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderChromaBlur->setValue( editSlider.getValue() );
}

//DoubleClick on DenoiseStrength Label
void MainWindow::on_label_DenoiseStrength_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDenoiseStrength, ui->label_DenoiseStrength, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderDenoiseStrength->setValue( editSlider.getValue() );
}

//DoubleClick on RbfDenoiseLuma Label
void MainWindow::on_label_RbfDenoiseLuma_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderRbfDenoiseLuma, ui->label_RbfDenoiseLuma, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderRbfDenoiseLuma->setValue( editSlider.getValue() );
}

//DoubleClick on RbfDenoiseChroma Label
void MainWindow::on_label_RbfDenoiseChroma_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderRbfDenoiseChroma, ui->label_RbfDenoiseChroma, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderRbfDenoiseChroma->setValue( editSlider.getValue() );
}

//DoubleClick on RbfDenoiseRange Label
void MainWindow::on_label_RbfDenoiseRange_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderRbfDenoiseRange, ui->label_RbfDenoiseRange, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderRbfDenoiseRange->setValue( editSlider.getValue() );
}

//DoubleClick on GrainStrength Label
void MainWindow::on_label_GrainStrength_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderGrainStrength, ui->label_GrainStrength, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderGrainStrength->setValue( editSlider.getValue() );
}

//Repaint audio if its size changed
void MainWindow::on_labelAudioTrack_sizeChanged()
{
    paintAudioTrack();
}

//DoubleClick on Lut Strength Label
void MainWindow::on_label_LutStrengthVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLutStrength, ui->label_LutStrengthVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderLutStrength->setValue( editSlider.getValue() );
}

//DoubleClick on Filter Strength Label
void MainWindow::on_label_FilterStrengthVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderFilterStrength, ui->label_FilterStrengthVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderFilterStrength->setValue( editSlider.getValue() );
}

//DoubleClick on Vignette Strength Label
void MainWindow::on_label_VignetteStrengthVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVignetteStrength, ui->label_VignetteStrengthVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVignetteStrength->setValue( editSlider.getValue() );
}

//DoubleClick on Vignette Radius Label
void MainWindow::on_label_VignetteRadiusVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVignetteRadius, ui->label_VignetteRadiusVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVignetteRadius->setValue( editSlider.getValue() );
}

//DoubleClick on Vignette Shape Label
void MainWindow::on_label_VignetteShapeVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVignetteShape, ui->label_VignetteShapeVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVignetteShape->setValue( editSlider.getValue() );
}

//DoubleClick on CA red Label
void MainWindow::on_label_CaRedVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderCaRed, ui->label_CaRedVal, 10.0, 1, 10.0 );
    editSlider.exec();
    ui->horizontalSliderCaRed->setValue( editSlider.getValue() );
}

//DoubleClick on CA blue Label
void MainWindow::on_label_CaBlueVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderCaBlue, ui->label_CaBlueVal, 10.0, 1, 10.0 );
    editSlider.exec();
    ui->horizontalSliderCaBlue->setValue( editSlider.getValue() );
}

//DoubleClick on CA desaturate Label
void MainWindow::on_label_CaDesaturateVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderCaDesaturate, ui->label_CaDesaturateVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderCaDesaturate->setValue( editSlider.getValue() );
}

//DoubleClick on CA radius Label
void MainWindow::on_label_CaRadiusVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderCaRadius, ui->label_CaRadiusVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderCaRadius->setValue( editSlider.getValue() );
}

void MainWindow::on_label_RawWhiteVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderRawWhite, ui->label_RawWhiteVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderRawWhite->setValue( editSlider.getValue() );
}

void MainWindow::on_label_RawBlackVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderRawBlack, ui->label_RawBlackVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderRawBlack->setValue( editSlider.getValue() );
}

void MainWindow::on_label_ToneVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderTone, ui->label_ToneVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderTone->setValue( editSlider.getValue() );
}

void MainWindow::on_label_ToningStrengthVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderToningStrength, ui->label_ToningStrengthVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderToningStrength->setValue( editSlider.getValue() );
}

void MainWindow::on_label_VidstabStepsizeVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVidstabStepsize, ui->label_VidstabStepsizeVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVidstabStepsize->setValue( editSlider.getValue() );
}

void MainWindow::on_label_VidstabShakinessVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVidstabShakiness, ui->label_VidstabShakinessVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVidstabShakiness->setValue( editSlider.getValue() );
}

void MainWindow::on_label_VidstabAccuracyVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVidstabAccuracy, ui->label_VidstabAccuracyVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVidstabAccuracy->setValue( editSlider.getValue() );
}

void MainWindow::on_label_VidstabZoomVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVidstabZoom, ui->label_VidstabZoomVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVidstabZoom->setValue( editSlider.getValue() );
}

void MainWindow::on_label_VidstabSmoothingVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderVidstabSmoothing, ui->label_VidstabSmoothingVal, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderVidstabSmoothing->setValue( editSlider.getValue() );
}

//Fullscreen Mode
void MainWindow::on_actionFullscreen_triggered( bool checked )
{
    static bool editWasActive;
    static bool sessionWasActive;
    static bool audioWasActive;

    if( checked )
    {
        ui->statusBar->hide();
        ui->mainToolBar->hide();
        ui->menuBar->hide();
        ui->horizontalSliderPosition->hide();
        ui->gridLayoutMain->setContentsMargins( 0, 0, 0, 0 );
        editWasActive = ui->actionShowEditArea->isChecked();
        sessionWasActive = ui->actionShowSessionArea->isChecked();
        audioWasActive = ui->actionShowAudioTrack->isChecked();
        ui->actionShowEditArea->setChecked( false );
        ui->actionShowSessionArea->setChecked( false );
        ui->actionShowAudioTrack->setChecked( false );
        ui->actionShowEditArea->setEnabled( false );
        ui->actionShowSessionArea->setEnabled( false );
        ui->actionShowAudioTrack->setEnabled( false );
        this->showFullScreen();
    }
    else
    {
        this->showNormal();
        ui->statusBar->show();
        ui->mainToolBar->show();
        ui->menuBar->show();
        ui->horizontalSliderPosition->show();
        ui->gridLayoutMain->setContentsMargins( 0, 5, 0, 5 );
        if( !ui->actionShowEditArea->isChecked() && editWasActive ) ui->actionShowEditArea->setChecked( true );
        if( !ui->actionShowSessionArea->isChecked() && sessionWasActive ) ui->actionShowSessionArea->setChecked( true );
        if( !ui->actionShowAudioTrack->isChecked() && audioWasActive ) ui->actionShowAudioTrack->setChecked( true );
        ui->actionShowEditArea->setEnabled( true );
        ui->actionShowSessionArea->setEnabled( true );
        ui->actionShowAudioTrack->setEnabled( true );
    }
    qApp->processEvents();
    m_frameChanged = true;
}

//Handles all export tasks, for batch export
//Must be called on export start
//Gets called when one export is ready
void MainWindow::exportHandler( void )
{
    static bool exportRunning = false;
    static int numberOfJobs = 1;
    static int jobNumber = 0;
    //Was started?
    if( exportRunning )
    {
        //Cut first job!
        if( !m_exportQueue.empty() ) //Only to avoid crashes
        {
            ReceiptSettings *receipt = m_exportQueue.takeFirst();
            delete receipt;
        }
    }
    else
    {
        //If not running save number of jobs
        numberOfJobs = m_exportQueue.count();
        m_exportAbortPressed = false;
        jobNumber = 0;
        int totalFrames = 0;
        for( int i = 0; i < numberOfJobs; i++ )
        {
            totalFrames += m_exportQueue.at(i)->cutOut() - m_exportQueue.at(i)->cutIn() + 1;
        }
        m_pStatusDialog->setTotalFrames( totalFrames );
        m_pStatusDialog->startExportTime();
    }
    //Are there jobs?
    if( !m_exportQueue.empty() )
    {
        //Next job!
        exportRunning = true;
        jobNumber++;
        //Open file and settings
        if( openMlv( m_exportQueue.first()->fileName() ) )
        {
            //auto skip corrupted file
            emit exportReady();
            return;
        }
        //Set sliders to receipt
        setSliders( m_exportQueue.first(), false );
        //Fill label in StatusDialog
        m_pStatusDialog->ui->label->setText( tr( "%1/%2 - %3" )
                                             .arg( jobNumber )
                                             .arg( numberOfJobs )
                                             .arg( QFileInfo( m_exportQueue.first()->fileName() ).fileName() ) );

        //Start it, raw/rendered
        if( m_codecProfile == CODEC_CDNG
         || m_codecProfile == CODEC_CDNG_LOSSLESS
         || m_codecProfile == CODEC_CDNG_FAST )
        {
            //raw output
            startExportCdng( m_exportQueue.first()->exportFileName() );
        }
#ifdef Q_OS_MACX
        else if( ( m_codecProfile == CODEC_PRORES422ST && m_codecOption == CODEC_PRORES_AVFOUNDATION )
              || ( m_codecProfile == CODEC_PRORES4444 && m_codecOption == CODEC_PRORES_AVFOUNDATION )
              || ( m_codecProfile == CODEC_H264 && m_codecOption == CODEC_H264_AVFOUNDATION )
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
              || ( m_codecProfile == CODEC_H265 && m_codecOption == CODEC_H265_AVFOUNDATION )
#endif
               )
        {
            //AVFoundation
            startExportAVFoundation( m_exportQueue.first()->exportFileName() );
        }
#endif
        else if( m_codecProfile == CODEC_MLV )
        {
            //MLV output
            startExportMlv( m_exportQueue.first()->exportFileName() );
        }
        else
        {
            //rendered output
            startExportPipe( m_exportQueue.first()->exportFileName() ); //Pipe export
        }
        return;
    }
    //Else if all planned exports are ready
    else
    {
        //Hide Status Dialog
        m_pStatusDialog->close();
        //Open last file which was opened before export
        openMlv( m_pSessionReceipts.at( m_lastClipBeforeExport )->fileName() );
        setSliders( m_pSessionReceipts.at( m_lastClipBeforeExport ), false );
        //Unblock GUI
        setEnabled( true );
        //Export is ready
        exportRunning = false;

        if( !m_exportAbortPressed )
        {
            //Start export script when ready
            m_pScripting->executePostExportScript();
            QMessageBox::information( this, tr( "Export" ), tr( "Export is ready." ) );
        }
        else QMessageBox::information( this, tr( "Export" ), tr( "Export aborted." ) );

        //Caching is in which state? Set it!
        if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
    }
}

//Play button pressed
void MainWindow::on_actionPlay_triggered(bool checked)
{
    //Last frame? Go to first frame!
    if( checked && ui->horizontalSliderPosition->value()+1 >= ui->spinBoxCutOut->value() )
    {
        on_actionGoto_First_Frame_triggered();
    }

    //If no audio, we have nothing to do here
    if( !doesMlvHaveAudio( m_pMlvObject ) ) return;

    if( !checked )
    {
        //Stop Audio
        m_pAudioPlayback->stop();
        qApp->processEvents();
    }
    else
    {
        //Start Audio
        if( ui->actionAudioOutput->isChecked()
         && ui->actionDropFrameMode->isChecked() )
        {
            m_tryToSyncAudio = true;
        }
    }
}

//Play button toggled (by program)
void MainWindow::on_actionPlay_toggled(bool checked)
{
    //When stopping, debayer selection has to come in right order from render thread (extra-invitation)
    if( !checked ) m_playbackStopped = true;

    selectDebayerAlgorithm();
}

//Zebras en-/disabled -> redraw
void MainWindow::on_actionShowZebras_triggered()
{
    m_frameChanged = true;
}

//Focus Pixel changed
void MainWindow::toolButtonFocusPixelsChanged( void )
{
    llrpSetFocusPixelMode( m_pMlvObject, toolButtonFocusPixelsCurrentIndex() );
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Focus Pixel Method changed
void MainWindow::toolButtonFocusPixelsIntMethodChanged( void )
{
    llrpSetFocusPixelInterpolationMethod( m_pMlvObject, toolButtonFocusPixelsIntMethodCurrentIndex() );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Bad Pixel changed
void MainWindow::toolButtonBadPixelsChanged( void )
{
    llrpSetBadPixelMode( m_pMlvObject, toolButtonBadPixelsCurrentIndex() );
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Bad Pixel Search Method changed
void MainWindow::toolButtonBadPixelsSearchMethodChanged()
{
    llrpSetBadPixelSearchMethod( m_pMlvObject, toolButtonBadPixelsSearchMethodCurrentIndex() );
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Bad Pixel Interpolation Method changed
void MainWindow::toolButtonBadPixelsIntMethodChanged( void )
{
    llrpSetBadPixelInterpolationMethod( m_pMlvObject, toolButtonBadPixelsIntMethodCurrentIndex() );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Chroma Smooth changed
void MainWindow::toolButtonChromaSmoothChanged( void )
{
    switch( toolButtonChromaSmoothCurrentIndex() )
    {
    case 0:
        llrpSetChromaSmoothMode(m_pMlvObject, CS_OFF);
        break;
    case 1:
        llrpSetChromaSmoothMode(m_pMlvObject, CS_2x2);
        break;
    case 2:
        llrpSetChromaSmoothMode(m_pMlvObject, CS_3x3);
        break;
    case 3:
        llrpSetChromaSmoothMode(m_pMlvObject, CS_5x5);
        break;
    default:
        llrpSetChromaSmoothMode(m_pMlvObject, CS_OFF);
    }
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Pattern Noise changed
void MainWindow::toolButtonPatternNoiseChanged( void )
{
    llrpSetPatternNoiseMode( m_pMlvObject, toolButtonPatternNoiseCurrentIndex() );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Upside Down Mode changed
void MainWindow::toolButtonUpsideDownChanged( void )
{
    processingSetTransformation( m_pProcessingObject, toolButtonUpsideDownCurrentIndex() );
    m_frameChanged = true;
}

//Vertical Stripes changed
void MainWindow::toolButtonVerticalStripesChanged( void )
{
    llrpSetVerticalStripeMode( m_pMlvObject, toolButtonVerticalStripesCurrentIndex() );
    llrpComputeStripesOn(m_pMlvObject);
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Value Deflicker Target changed
void MainWindow::on_spinBoxDeflickerTarget_valueChanged(int arg1)
{
    llrpSetDeflickerTarget(m_pMlvObject, arg1);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Dual iso force button toggled
void MainWindow::on_toolButtonDualIsoForce_toggled( bool checked )
{
    if( llrpGetDualIsoValidity( m_pMlvObject ) == DISO_VALID )
    {
        ui->DualISOLabel->setEnabled( true );
        ui->toolButtonDualIsoOff->setEnabled( true );
        ui->toolButtonDualIsoOn->setEnabled( true );
        ui->toolButtonDualIsoPreview->setEnabled( true );
    }
    else
    {
        ui->DualISOLabel->setEnabled( checked );
        ui->toolButtonDualIsoOff->setEnabled( checked );
        ui->toolButtonDualIsoOn->setEnabled( checked );
        ui->toolButtonDualIsoPreview->setEnabled( checked );
        llrpSetDualIsoValidity( m_pMlvObject, checked );

        if( !checked )
        {
            setToolButtonDualIso( false );
        }
    }
}

//DualISO changed
void MainWindow::toolButtonDualIsoChanged( void )
{
    if(!m_fileLoaded) return;

    //In preview mode, the other dualIso options are grayed out
    if( ( toolButtonDualIsoCurrentIndex() == 1 ) && ui->checkBoxRawFixEnable->isChecked() )
    {
        ui->toolButtonDualIsoInterpolation->setEnabled( true );
        ui->toolButtonDualIsoAliasMap->setEnabled( true );
        ui->toolButtonDualIsoFullresBlending->setEnabled( true );
        ui->DualISOInterpolationLabel->setEnabled( true );
        ui->DualISOAliasMapLabel->setEnabled( true );
    }
    else
    {
        ui->toolButtonDualIsoInterpolation->setEnabled( false );
        ui->toolButtonDualIsoAliasMap->setEnabled( false );
        ui->toolButtonDualIsoFullresBlending->setEnabled( false );
        ui->DualISOInterpolationLabel->setEnabled( false );
        ui->DualISOAliasMapLabel->setEnabled( false );
    }

    //Set dualIso mode
    llrpSetDualIsoMode( m_pMlvObject, toolButtonDualIsoCurrentIndex() );
    //Reset processing black and white levels
    processingSetBlackAndWhiteLevel( m_pMlvObject->processing, getMlvBlackLevel( m_pMlvObject ), getMlvWhiteLevel( m_pMlvObject ), getMlvBitdepth( m_pMlvObject ) );
    //Reset diso levels to mlv raw levels
    llrpResetDngBWLevels( m_pMlvObject );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//DualISO Interpolation changed
void MainWindow::toolButtonDualIsoInterpolationChanged( void )
{
    llrpSetDualIsoInterpolationMethod( m_pMlvObject, toolButtonDualIsoInterpolationCurrentIndex() );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//DualISO Alias Map changed
void MainWindow::toolButtonDualIsoAliasMapChanged( void )
{
    llrpSetDualIsoAliasMapMode( m_pMlvObject, toolButtonDualIsoAliasMapCurrentIndex() );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//DualISO Fullres Blending changed
void MainWindow::toolButtonDualIsoFullresBlendingChanged( void )
{
    llrpSetDualIsoFullResBlendingMode( m_pMlvObject, toolButtonDualIsoFullresBlendingCurrentIndex() );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Darkframe Subtraction On/Off changed
void MainWindow::toolButtonDarkFrameSubtractionChanged( bool checked )
{
    if( !checked ) return;
    //Set dark frame mode to llrawproc struct
    llrpSetDarkFrameMode( m_pMlvObject, toolButtonDarkFrameSubtractionCurrentIndex() );
    //Blocking filename while Ext or Int mode is active
    if( toolButtonDarkFrameSubtractionCurrentIndex() > 0 )
    {
        ui->lineEditDarkFrameFile->setEnabled( false );
        ui->toolButtonDarkFrameSubtractionFile->setEnabled( false );
    }
    else
    {
        ui->lineEditDarkFrameFile->setEnabled( true );
        ui->toolButtonDarkFrameSubtractionFile->setEnabled( true );
    }
    //Force bad pixels and stripes calculation b/c dark frame processing happens before
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Selection of gradation curve
void MainWindow::toolButtonGCurvesChanged( void )
{
    if( toolButtonGCurvesCurrentIndex() == 0 ) ui->labelCurves->setActiveLine( LINENR_W );
    else if( toolButtonGCurvesCurrentIndex() == 1 ) ui->labelCurves->setActiveLine( LINENR_R );
    else if( toolButtonGCurvesCurrentIndex() == 2 ) ui->labelCurves->setActiveLine( LINENR_G );
    else ui->labelCurves->setActiveLine( LINENR_B );
}

//Reset the gradation curves
void MainWindow::on_toolButtonGCurvesReset_clicked()
{
    ui->labelCurves->resetLines();
    ui->labelCurves->paintElement();
}

//Reset one gradation curve
void MainWindow::on_toolButtonGCurvesResetOne_clicked()
{
    ui->labelCurves->resetCurrentLine();
    ui->labelCurves->paintElement();
}

//Reset HueVsHue curve
void MainWindow::on_toolButtonHueVsHueReset_clicked()
{
    ui->labelHueVsHue->resetLine();
    ui->labelHueVsHue->paintElement();
}

//Reset HueVsSat curve
void MainWindow::on_toolButtonHueVsSatReset_clicked()
{
    ui->labelHueVsSat->resetLine();
    ui->labelHueVsSat->paintElement();
}

//Reset HueVsLuma curve
void MainWindow::on_toolButtonHueVsLumaReset_clicked()
{
    ui->labelHueVsLuma->resetLine();
    ui->labelHueVsLuma->paintElement();
}

//Reset LumaVsSat curve
void MainWindow::on_toolButtonLumaVsSatReset_clicked()
{
    ui->labelLumaVsSat->resetLine();
    ui->labelLumaVsSat->paintElement();
}

//Goto next frame
void MainWindow::on_actionNextFrame_triggered()
{
    ui->horizontalSliderPosition->setValue( ui->horizontalSliderPosition->value() + 1 );
}

//Goto previous frame
void MainWindow::on_actionPreviousFrame_triggered()
{
    ui->horizontalSliderPosition->setValue( ui->horizontalSliderPosition->value() - 1 );
}

//En-/disable all raw corrections
void MainWindow::on_checkBoxRawFixEnable_clicked(bool checked)
{
    //Set llrawproc en-/disable here
    llrpSetFixRawMode( m_pMlvObject, (int)checked );
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;

    //Set GUI elements
    ui->FocusPixelsLabel->setEnabled( checked );
    ui->FocusPixelsInterpolationMethodLabel->setEnabled( checked );
    ui->BadPixelsLabel->setEnabled( checked );
    ui->BadPixelsInterpolationMethodLabel->setEnabled( checked );
    ui->ChromaSmoothLabel->setEnabled( checked );
    ui->PatternNoiseLabel->setEnabled( checked );
    ui->VerticalStripesLabel->setEnabled( checked );
    ui->DeflickerTargetLabel->setEnabled( checked );
    ui->DualISOLabel->setEnabled( checked && ( llrpGetDualIsoValidity( m_pMlvObject ) > 0 ) );
    ui->DualISOInterpolationLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualISOAliasMapLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualISOFullresBlendingLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->FocusPixelsInterpolationMethodLabel_2->setEnabled( checked );

    ui->toolButtonFocusDots->setEnabled( checked );
    ui->toolButtonFocusDotInterpolation->setEnabled( checked );
    ui->toolButtonBadPixels->setEnabled( checked );
    ui->toolButtonBadPixelsInterpolation->setEnabled( checked );
    ui->toolButtonChroma->setEnabled( checked );
    ui->toolButtonPatternNoise->setEnabled( checked );
    ui->toolButtonVerticalStripes->setEnabled( checked );
    ui->toolButtonDualIso->setEnabled( checked );
    ui->toolButtonDualIsoForce->setEnabled( checked );
    ui->toolButtonDualIsoInterpolation->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->toolButtonDualIsoAliasMap->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->toolButtonDualIsoFullresBlending->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->spinBoxDeflickerTarget->setEnabled( checked );
    ui->toolButtonBadPixelsSearchMethodNormal->setEnabled( checked );
    ui->toolButtonBadPixelsSearchMethodAggressive->setEnabled( checked );
    ui->labelDarkFrameSubtraction->setEnabled( checked );
    ui->toolButtonDarkFrameSubtraction->setEnabled( checked );
    ui->toolButtonDarkFrameSubtractionFile->setEnabled( checked );
    ui->lineEditDarkFrameFile->setEnabled( checked );

    ui->RawBlackLabel->setEnabled( checked );
    ui->horizontalSliderRawBlack->setEnabled( checked );
    ui->label_RawBlackVal->setEnabled( checked );
    ui->RawWhiteLabel->setEnabled( checked );
    ui->horizontalSliderRawWhite->setEnabled( checked );
    ui->label_RawWhiteVal->setEnabled( checked );
    on_horizontalSliderRawBlack_valueChanged( ui->horizontalSliderRawBlack->value() );
    on_horizontalSliderRawWhite_valueChanged( ui->horizontalSliderRawWhite->value() );
}

//En-/disable all LUT processing
void MainWindow::on_checkBoxLutEnable_clicked(bool checked)
{
    if( checked ) processingEnableLut( m_pProcessingObject );
    else processingDisableLut( m_pProcessingObject );
    m_frameChanged = true;

    ui->toolButtonLoadLut->setEnabled( checked );
    ui->lineEditLutName->setEnabled( checked );
    ui->label_LutStrengthText->setEnabled( checked );
    ui->label_LutStrengthVal->setEnabled( checked );
    ui->horizontalSliderLutStrength->setEnabled( checked );
}

//En-/disable all filter processing
void MainWindow::on_checkBoxFilterEnable_clicked(bool checked)
{
    if( checked ) processingEnableFilters( m_pProcessingObject );
    else processingDisableFilters( m_pProcessingObject );
    m_frameChanged = true;

    ui->comboBoxFilterName->setEnabled( checked );
    ui->label_FilterStrengthVal->setEnabled( checked );
    ui->label_FilterStrengthText->setEnabled( checked );
    ui->horizontalSliderFilterStrength->setEnabled( checked );
}

//En-/disable ffmpeg vidstab video stabilizer
void MainWindow::on_checkBoxVidstabEnable_toggled(bool checked)
{
    ui->checkBoxVidstabTripod->setEnabled( checked );

    //Enable/Disable UI elements
    if( ui->checkBoxVidstabTripod->isChecked() ) checked = false;
    ui->horizontalSliderVidstabStepsize->setEnabled( checked );
    ui->horizontalSliderVidstabShakiness->setEnabled( checked );
    ui->horizontalSliderVidstabAccuracy->setEnabled( checked );
    ui->horizontalSliderVidstabZoom->setEnabled( checked );
    ui->horizontalSliderVidstabSmoothing->setEnabled( checked );
    ui->label_VidstabStepsizeText->setEnabled( checked );
    ui->label_VidstabStepsizeVal->setEnabled( checked );
    ui->label_VidstabShakinessText->setEnabled( checked );
    ui->label_VidstabShakinessVal->setEnabled( checked );
    ui->label_VidstabAccuracyText->setEnabled( checked );
    ui->label_VidstabAccuracyVal->setEnabled( checked );
    ui->label_VidstabZoomText->setEnabled( checked );
    ui->label_VidstabZoomVal->setEnabled( checked );
    ui->label_VidstabSmoothingText->setEnabled( checked );
    ui->label_VidstabSmoothingVal->setEnabled( checked );
}

//En-/disable ffmpeg vidstab tripod mode
void MainWindow::on_checkBoxVidstabTripod_toggled(bool checked)
{
    if( !ui->checkBoxVidstabEnable->isChecked() ) checked = true;
    ui->horizontalSliderVidstabStepsize->setEnabled( !checked );
    ui->horizontalSliderVidstabShakiness->setEnabled( !checked );
    ui->horizontalSliderVidstabAccuracy->setEnabled( !checked );
    ui->horizontalSliderVidstabZoom->setEnabled( !checked );
    ui->horizontalSliderVidstabSmoothing->setEnabled( !checked );
    ui->label_VidstabStepsizeText->setEnabled( !checked );
    ui->label_VidstabStepsizeVal->setEnabled( !checked );
    ui->label_VidstabShakinessText->setEnabled( !checked );
    ui->label_VidstabShakinessVal->setEnabled( !checked );
    ui->label_VidstabAccuracyText->setEnabled( !checked );
    ui->label_VidstabAccuracyVal->setEnabled( !checked );
    ui->label_VidstabZoomText->setEnabled( !checked );
    ui->label_VidstabZoomVal->setEnabled( !checked );
    ui->label_VidstabSmoothingText->setEnabled( !checked );
    ui->label_VidstabSmoothingVal->setEnabled( !checked );
}

//Activate & Deactivate wbPicker
void MainWindow::on_actionWhiteBalancePicker_toggled(bool checked)
{
    ui->graphicsView->setWbPickerActive( checked );
    m_pScene->setWbPickerActive( checked );
    m_pGradientElement->setMovable( !checked );
    ui->toolButtonGradientPaint->setChecked( false );
}

//wb picking ready
void MainWindow::whiteBalancePicked( int x, int y )
{
    //ui->actionWhiteBalancePicker->setChecked( false ); //Single Click
    on_actionWhiteBalancePicker_toggled( true ); //Click until deactivation

    //Quit if no mlv loaded
    if( !m_fileLoaded ) return;

    //Some math if in stretch (fit) mode
    if( ui->actionZoomFit->isChecked() )
    {
        x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
        y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();
    }
    else
    {
        x /= getHorizontalStretchFactor(false);
        y /= getVerticalStretchFactor(false);
    }

    //Quit if click not in picture
    if( x < 0 || y < 0 || x > getMlvWidth( m_pMlvObject ) || y > getMlvHeight( m_pMlvObject ) ) return;

    int temp, tint;
    //qDebug() << "Click in Scene:" << x << y;
    findMlvWhiteBalance( m_pMlvObject, ui->horizontalSliderPosition->value(), x, y, &temp, &tint, m_wbMode );
    //qDebug() << "wbTemp:" << temp << "wbTint" << tint;
    ui->horizontalSliderTemperature->setValue( temp );
    on_horizontalSliderTemperature_valueChanged( temp );
    ui->horizontalSliderTint->setValue( tint );
    on_horizontalSliderTint_valueChanged( tint );
}

//WB Picker Type change
void MainWindow::on_toolButtonWbMode_clicked()
{
    if( m_wbMode )
    {
        m_wbMode = 0;
        ui->toolButtonWbMode->setIcon( QIcon( ":/IMG/IMG/Grey-Ball-icon.png" ) );
        ui->toolButtonWbMode->setToolTip( tr( "WB picker on grey" ) );
    }
    else
    {
        m_wbMode = 1;
        ui->toolButtonWbMode->setIcon( QIcon( ":/RetinaIMG/RetinaIMG/face.png" ) );
        ui->toolButtonWbMode->setToolTip( tr( "WB picker on skin" ) );
    }
}

//Gradient anchor was selected by user
void MainWindow::gradientAnchorPicked(int x, int y)
{
    ui->checkBoxGradientEnable->setChecked( true );
    //Some math if in stretch (fit) mode
    x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
    y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();

    m_pGradientElement->reset();
    m_pGradientElement->setStartPos( x, y );

    ui->spinBoxGradientX->blockSignals( true );
    ui->spinBoxGradientY->blockSignals( true );
    ui->spinBoxGradientX->setValue( x );
    ui->spinBoxGradientY->setValue( y );
    ui->spinBoxGradientX->blockSignals( false );
    ui->spinBoxGradientY->blockSignals( false );
}

//Gradient final position was selected by user
void MainWindow::gradientFinalPosPicked(int x, int y, bool isFinished)
{
    //Get both positions
    QPointF endPos = QPointF( x * getMlvWidth( m_pMlvObject ) / m_pScene->width(),
                              y * getMlvHeight( m_pMlvObject ) / m_pScene->height() );
    //Some math
    m_pGradientElement->setFinalPos( endPos.x(), endPos.y() );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->show();

    //Set the UI numbers and sliders
    ui->labelGradientAngle->setText( QString( "%1°" ).arg( m_pGradientElement->uiAngle(), 0, 'f', 1 ) );
    ui->dialGradientAngle->blockSignals( true );
    ui->dialGradientAngle->setValue( m_pGradientElement->uiAngle() * 10.0 );
    ui->dialGradientAngle->blockSignals( false );
    ui->spinBoxGradientLength->blockSignals( true );
    ui->spinBoxGradientLength->setValue( m_pGradientElement->uiLength() );
    ui->spinBoxGradientLength->blockSignals( false );

    //If action finished, uncheck paint button
    if( isFinished )
    {
        ui->toolButtonGradientPaint->setChecked( false );
    }

    setGradientMask();
}

//Collapse & Expand Raw Correction
void MainWindow::on_groupBoxRawCorrection_toggled(bool arg1)
{
    ui->frameRawCorrection->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxRawCorrection->setMaximumHeight( 30 );
    else ui->groupBoxRawCorrection->setMaximumHeight( 16777215 );
}

//Collapse & Expand Cut In Out
void MainWindow::on_groupBoxCutInOut_toggled(bool arg1)
{
    ui->frameCutInOut->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxCutInOut->setMaximumHeight( 30 );
    else ui->groupBoxCutInOut->setMaximumHeight( 16777215 );
}

//Collapse & Expand Debayer
void MainWindow::on_groupBoxDebayer_toggled(bool arg1)
{
    ui->frameDebayer->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxDebayer->setMaximumHeight( 30 );
    else ui->groupBoxDebayer->setMaximumHeight( 16777215 );
}

void MainWindow::on_groupBoxProfiles_toggled(bool arg1)
{
    ui->frameProfiles->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxProfiles->setMaximumHeight( 30 );
    else ui->groupBoxProfiles->setMaximumHeight( 16777215 );
}

//Collapse & Expand Processing
void MainWindow::on_groupBoxProcessing_toggled(bool arg1)
{
    ui->frameProcessing->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxProcessing->setMaximumHeight( 30 );
    else ui->groupBoxProcessing->setMaximumHeight( 16777215 );
}

//Collapse & Expand Details
void MainWindow::on_groupBoxDetails_toggled(bool arg1)
{
    ui->frameDetails->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxDetails->setMaximumHeight( 30 );
    else ui->groupBoxDetails->setMaximumHeight( 16777215 );
}

//Collapse & Expand HSL box
void MainWindow::on_groupBoxHsl_toggled(bool arg1)
{
    ui->frameHsl->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxHsl->setMaximumHeight( 30 );
    else ui->groupBoxHsl->setMaximumHeight( 16777215 );
}

//Collapse & Expand Toning
void MainWindow::on_groupBoxToning_toggled(bool arg1)
{
    ui->frameToning->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxToning->setMaximumHeight( 30 );
    else ui->groupBoxToning->setMaximumHeight( 16777215 );
}

//Collapse & Expand Color Wheels
void MainWindow::on_groupBoxColorWheels_toggled(bool arg1)
{
    ui->frameColorWheels->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxColorWheels->setMaximumHeight( 30 );
    else ui->groupBoxColorWheels->setMaximumHeight( 16777215 );
}

//Collapse & Expand LUT
void MainWindow::on_groupBoxLut_toggled(bool arg1)
{
    ui->frameLut->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxLut->setMaximumHeight( 30 );
    else ui->groupBoxLut->setMaximumHeight( 16777215 );
}

//Collapse & Expand Filter
void MainWindow::on_groupBoxFilter_toggled(bool arg1)
{
    ui->frameFilter->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxFilter->setMaximumHeight( 30 );
    else ui->groupBoxFilter->setMaximumHeight( 16777215 );
}

//Collapse & Expand Vignette
void MainWindow::on_groupBoxVignette_toggled(bool arg1)
{
    ui->frameVignette->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxVignette->setMaximumHeight( 30 );
    else ui->groupBoxVignette->setMaximumHeight( 16777215 );
}

//Collapse & Expand Linear Gradient
void MainWindow::on_groupBoxLinearGradient_toggled(bool arg1)
{
    ui->frameGradient->setVisible( arg1 );
    if( !arg1 )
    {
        ui->groupBoxLinearGradient->setMaximumHeight( 30 );
        m_pGradientElement->gradientGraphicsElement()->hide();
    }
    else
    {
        if( ui->checkBoxGradientEnable->isChecked() ) m_pGradientElement->gradientGraphicsElement()->show();
        ui->groupBoxLinearGradient->setMaximumHeight( 16777215 );
    }
}

//Collapse & Expand Viewer
void MainWindow::on_groupBoxTransformation_toggled(bool arg1)
{
    ui->frameAspectRatio->setVisible( arg1 );
    if( !arg1 ) ui->groupBoxTransformation->setMaximumHeight( 30 );
    else ui->groupBoxTransformation->setMaximumHeight( 16777215 );
}

//Abort pressed while exporting
void MainWindow::exportAbort( void )
{
    m_exportAbortPressed = true;
    m_exportQueue.clear();
}

//Draw the frame when render thread is ready
void MainWindow::drawFrameReady()
{
    Qt::TransformationMode mode = Qt::FastTransformation;
    if( !ui->actionPlay->isChecked()
     || ui->actionUseNoneDebayer->isChecked()
     || ui->actionCaching->isChecked() )
    {
        mode = Qt::SmoothTransformation;
    }

    if( ui->actionZoomFit->isChecked() )
    {
        //Some math to have the picture exactly in the frame
        int actWidth;
        int actHeight;
        if( ui->actionFullscreen->isChecked() )
        {
            actWidth = QApplication::primaryScreen()->size().width();
            actHeight = QApplication::primaryScreen()->size().height();
        }
        else
        {
            actWidth = ui->graphicsView->width();
            actHeight = ui->graphicsView->height();
        }
        int desWidth = actWidth;
        int desHeight = actWidth * getMlvHeight(m_pMlvObject) / getMlvWidth(m_pMlvObject) * getVerticalStretchFactor(false) / getHorizontalStretchFactor(false);
        if( desHeight > actHeight )
        {
            desHeight = actHeight;
            desWidth = actHeight * getMlvWidth(m_pMlvObject) / getMlvHeight(m_pMlvObject) / getVerticalStretchFactor(false) * getHorizontalStretchFactor(false);
        }

        //Get Picture


        QPixmap pic = QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                                          .scaled( desWidth * devicePixelRatio(),
                                                   desHeight * devicePixelRatio(),
                                                   Qt::IgnoreAspectRatio, mode) );
        //Set Picture to Retina
        pic.setDevicePixelRatio( devicePixelRatio() );
        //Bring frame to GUI (fit to window)
        m_pGraphicsItem->setPixmap( pic );
        //Set Scene
        m_pScene->setSceneRect( 0, 0, desWidth, desHeight );
    }
    else
    {
        //Bring frame to GUI (100%)
        if( getVerticalStretchFactor(false) == 1.0
         && getHorizontalStretchFactor(false) == 1.0 ) //Fast mode for 1.0 stretch factor
        {
            m_pGraphicsItem->setPixmap( QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 ) ) );
            m_pScene->setSceneRect( 0, 0, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject) );
        }
        else
        {
            QPixmap pixmap;
            //Qvir resize
            if( mode == Qt::SmoothTransformation && ui->actionBetterResizer->isChecked() )
            {
                uint8_t *scaledPic = (uint8_t*)malloc( 3 * getMlvWidth(m_pMlvObject) * getHorizontalStretchFactor(false)
                                                         * getMlvHeight(m_pMlvObject) * getVerticalStretchFactor(false)
                                                         * sizeof( uint8_t ) );
                avir_scale_thread_pool scaling_pool;
                avir::CImageResizerVars vars; vars.ThreadPool = &scaling_pool;
                avir::CImageResizerParamsUltra roptions;
                avir::CImageResizer<> image_resizer( 8, 0, roptions );
                image_resizer.resizeImage( m_pRawImage,
                                           getMlvWidth(m_pMlvObject),
                                           getMlvHeight(m_pMlvObject), 0,
                                           scaledPic,
                                           getMlvWidth(m_pMlvObject) * getHorizontalStretchFactor(false),
                                           getMlvHeight(m_pMlvObject) * getVerticalStretchFactor(false),
                                           3, 0, &vars );
                pixmap = QPixmap::fromImage( QImage( ( unsigned char *) scaledPic,
                                                     getMlvWidth(m_pMlvObject) * getHorizontalStretchFactor(false),
                                                     getMlvHeight(m_pMlvObject) * getVerticalStretchFactor(false),
                                                     QImage::Format_RGB888 ) );
                free( scaledPic );
            }
            //Qt resize
            else
            {
                pixmap = QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                                             .scaled( getMlvWidth(m_pMlvObject) * getHorizontalStretchFactor(false),
                                                      getMlvHeight(m_pMlvObject) * getVerticalStretchFactor(false),
                                                      Qt::IgnoreAspectRatio, mode) );
            }
            m_pGraphicsItem->setPixmap( pixmap );
            m_pScene->setSceneRect( 0, 0, getMlvWidth(m_pMlvObject) * getHorizontalStretchFactor(false), getMlvHeight(m_pMlvObject) * getVerticalStretchFactor(false) );
        }
    }

    //Add zebras on the image
    uint8_t underOver = drawZebras();
    //Bring over/under to histogram
    bool under = false;
    bool over = false;
    if( ( underOver & 0x01 ) == 0x01 ) under = true;
    if( ( underOver & 0x02 ) == 0x02 ) over = true;

    //GetHistogram
    if( ui->actionShowHistogram->isChecked() )
    {
        ui->labelScope->setScope( m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), under, over, ScopesLabel::ScopeHistogram );
    }
    //Waveform
    else if( ui->actionShowWaveFormMonitor->isChecked() )
    {
        ui->labelScope->setScope( m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), under, over, ScopesLabel::ScopeWaveForm );
    }
    //Parade
    else if( ui->actionShowParade->isChecked() )
    {
        ui->labelScope->setScope( m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), under, over, ScopesLabel::ScopeRgbParade);
    }
    //VectorScope
    else if( ui->actionShowVectorScope->isChecked() )
    {
        ui->labelScope->setScope( m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), under, over, ScopesLabel::ScopeVectorScope );
    }

    //Drawing ready, next frame can be rendered
    m_frameStillDrawing = false;

    //Sync Audio
    if( m_tryToSyncAudio && ui->actionAudioOutput->isChecked() && ui->actionPlay->isChecked() && ui->actionDropFrameMode->isChecked() )
    {
        m_tryToSyncAudio = false;
        m_pAudioPlayback->stop();
        m_pAudioPlayback->jumpToPos( m_newPosDropMode );
        m_pAudioPlayback->play();
    }

    //And show the user which frame we show
    drawFrameNumberLabel();

    //Set frame to the middle
    if( m_zoomTo100Center )
    {
        m_zoomTo100Center = false;
        ui->graphicsView->horizontalScrollBar()->setValue( ( getMlvWidth(m_pMlvObject) * getHorizontalStretchFactor(false) - ui->graphicsView->width() ) / 2 );
        ui->graphicsView->verticalScrollBar()->setValue( ( getMlvHeight(m_pMlvObject) * getVerticalStretchFactor(false) - ui->graphicsView->height() ) / 2 );
    }

    //If zoom mode changed, redraw gradient element to the new size
    if( m_zoomModeChanged )
    {
        m_zoomModeChanged = false;
        m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                                   m_pScene->height(),
                                                   getMlvWidth( m_pMlvObject ),
                                                   getMlvHeight( m_pMlvObject ) );
    }

    //One more frame if stopped
    if( m_playbackStopped == true )
    {
        selectDebayerAlgorithm();
        m_playbackStopped = false;
    }

    //Reset delete clip action as enabled
    ui->actionDeleteSelectedClips->setEnabled( true );

    emit frameReady();
}

//Paintmode for gradient enabled/disabled
void MainWindow::on_toolButtonGradientPaint_toggled(bool checked)
{
    if( !checked )
    {
        ui->graphicsView->setCrossCursorActive( false ); // has to be done first
        ui->graphicsView->setDragMode( QGraphicsView::ScrollHandDrag );
        m_pGradientElement->gradientGraphicsElement()->show();
    }
    else
    {
        m_pGradientElement->gradientGraphicsElement()->hide();
        ui->graphicsView->setDragMode( QGraphicsView::NoDrag );
        ui->graphicsView->setCrossCursorActive( true ); // has to be done last
    }
    m_pScene->setGradientAdjustment( checked );
}

//Gradient Enable checked/unchecked
void MainWindow::on_checkBoxGradientEnable_toggled(bool checked)
{
    if( checked && ui->groupBoxLinearGradient->isChecked() ) m_pGradientElement->gradientGraphicsElement()->show();
    else m_pGradientElement->gradientGraphicsElement()->hide();

    processingSetGradientEnable( m_pProcessingObject, checked );

    m_frameChanged = true;
}

//The gradient startPoint X has changed
void MainWindow::on_spinBoxGradientX_valueChanged(int arg1)
{
    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setStartPos( arg1, ui->spinBoxGradientY->value() );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//The gradient startPoint Y has changed
void MainWindow::on_spinBoxGradientY_valueChanged(int arg1)
{
    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setStartPos( ui->spinBoxGradientX->value(), arg1 );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//The gradient length has changed
void MainWindow::on_spinBoxGradientLength_valueChanged(int arg1)
{
    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setUiLength( arg1 );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//The gradient angle label was doubleclicked
void MainWindow::on_labelGradientAngle_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.ui->doubleSpinBox->setMinimum( -179.9 );
    editSlider.ui->doubleSpinBox->setMaximum( 180.0 );
    editSlider.ui->doubleSpinBox->setDecimals( 1 );
    editSlider.ui->doubleSpinBox->setSingleStep( 0.1 );
    QString valString = ui->labelGradientAngle->text();
    valString.chop(1);
    editSlider.ui->doubleSpinBox->setValue( valString.toDouble() );
    editSlider.ui->doubleSpinBox->selectAll();
    QPoint pos;
    pos.setX(0);
    pos.setY(0);
    pos = ui->labelGradientAngle->mapToGlobal( pos );
    editSlider.setGeometry( pos.x(), pos.y(), 80, 20 );
    editSlider.exec();
    ui->dialGradientAngle->setValue( editSlider.getValue() * 10.0 );
}

//The gradient angle dial was turned
void MainWindow::on_dialGradientAngle_valueChanged(int value)
{
    ui->labelGradientAngle->setText( QString( "%1°" ).arg( value / 10.0, 0, 'f', 1 ) );

    m_pGradientElement->gradientGraphicsElement()->blockSignals( true );
    m_pGradientElement->setUiAngle( value / 10.0 );
    m_pGradientElement->redrawGradientElement( m_pScene->width(),
                                               m_pScene->height(),
                                               getMlvWidth( m_pMlvObject ),
                                               getMlvHeight( m_pMlvObject ) );
    m_pGradientElement->gradientGraphicsElement()->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//Someone moved the gradient graphics element
void MainWindow::gradientGraphicElementMoved(int x, int y)
{
    //Some math if in stretch (fit) mode
    x *= getMlvWidth( m_pMlvObject ) / m_pScene->width();
    y *= getMlvHeight( m_pMlvObject ) / m_pScene->height();

    m_pGradientElement->setStartPos( x, y );

    ui->spinBoxGradientX->blockSignals( true );
    ui->spinBoxGradientY->blockSignals( true );
    ui->spinBoxGradientX->setValue( x );
    ui->spinBoxGradientY->setValue( y );
    ui->spinBoxGradientX->blockSignals( false );
    ui->spinBoxGradientY->blockSignals( false );

    //Send to processing module
    setGradientMask();
}

//Someone starts/stops hovering the element
void MainWindow::gradientGraphicElementHovered(bool isHovered)
{
    //We don't want to see hovering if wb picker is enabled
    if( ui->actionWhiteBalancePicker->isChecked() ) isHovered = false;

    //Change color of grading elements to show the user it is hovered
    QPen pen;
    if( isHovered ) pen = QPen( Qt::yellow );
    else pen = QPen( Qt::white );
    pen.setWidth( 0 );
    m_pGradientElement->gradientGraphicsElement()->setPen( pen );
}

//Init the CutIn/Out elements with frames of clip
void MainWindow::initCutInOut(int frames)
{
    if( frames == -1 )
    {
        ui->spinBoxCutIn->setMinimum( 0 );
        ui->spinBoxCutIn->setMaximum( 0 );
        ui->spinBoxCutIn->setValue( 0 );
        ui->spinBoxCutOut->setMinimum( 0 );
        ui->spinBoxCutOut->setMaximum( 0 );
        ui->spinBoxCutOut->setValue( 0 );
    }
    else
    {
        ui->spinBoxCutIn->setMinimum( 1 );
        ui->spinBoxCutIn->setMaximum( frames );
        //ui->spinBoxCutIn->setValue( 1 );
        ui->spinBoxCutOut->setMinimum( 1 );
        ui->spinBoxCutOut->setMaximum( frames );
        //ui->spinBoxCutOut->setValue( frames );
    }
}

//Set the sliders to what is saved into the mlv
void MainWindow::initRawBlackAndWhite()
{
    ui->horizontalSliderRawBlack->blockSignals( true );
    ui->horizontalSliderRawBlack->setMaximum( ( 2 << ( getMlvBitdepth( m_pMlvObject ) - 1 ) ) - 1 );
    ui->horizontalSliderRawBlack->blockSignals( false );
    ui->horizontalSliderRawWhite->blockSignals( true );
    ui->horizontalSliderRawWhite->setMaximum( ( 2 << ( getMlvBitdepth( m_pMlvObject ) - 1 ) ) - 1 );
    ui->horizontalSliderRawWhite->setValue( ( 2 << ( getMlvBitdepth( m_pMlvObject ) - 1 ) ) - 1 ); //set value to max, because otherwise the new black value is blocked by old white value
    ui->horizontalSliderRawWhite->blockSignals( false );
    ui->horizontalSliderRawBlack->setValue( getMlvOriginalBlackLevel( m_pMlvObject ) );
    on_horizontalSliderRawBlack_valueChanged( getMlvOriginalBlackLevel( m_pMlvObject ) );
    ui->horizontalSliderRawWhite->setValue( getMlvOriginalWhiteLevel( m_pMlvObject ) );
    on_horizontalSliderRawWhite_valueChanged( getMlvOriginalWhiteLevel( m_pMlvObject ) );
}

//Get the current horizontal stretch factor
double MainWindow::getHorizontalStretchFactor( bool downScale )
{
    double factor = 1.0;
    if( ui->comboBoxHStretch->currentIndex() == 0 ) factor = STRETCH_H_100;
    else if( ui->comboBoxHStretch->currentIndex() == 1 ) factor = STRETCH_H_133;
    else if( ui->comboBoxHStretch->currentIndex() == 2 ) factor = STRETCH_H_150;
    else if( ui->comboBoxHStretch->currentIndex() == 3 ) factor = STRETCH_H_175;
    else if( ui->comboBoxHStretch->currentIndex() == 4 ) factor = STRETCH_H_180;
    else factor = STRETCH_H_200;

    if( ui->comboBoxVStretch->currentIndex() == 3 && !downScale ) factor *= 3.0;

    return factor;
}

//Get the current vertical stretch factor
double MainWindow::getVerticalStretchFactor( bool downScale )
{
    if( ui->comboBoxVStretch->currentIndex() == 0 ) return STRETCH_V_100;
    else if( ui->comboBoxVStretch->currentIndex() == 1 ) return STRETCH_V_167;
    else if( ui->comboBoxVStretch->currentIndex() == 2 ) return STRETCH_V_300;
    else
    {
        if( downScale )
            return STRETCH_V_033;
        else
            return STRETCH_V_100;
    }
}

//Read Whitebalance Info from MLV and setup slider
void MainWindow::setWhiteBalanceFromMlv(ReceiptSettings *sliders)
{
    switch( getMlvWbMode( m_pMlvObject ) )
    {
    case 0: //Auto - use default
    case 6: //Custom - use default
        sliders->setTemperature( 6000 );
        break;
    case 1: //Sunny
        sliders->setTemperature( 5200 );
        break;
    case 8: //Shade
        sliders->setTemperature( 7000 );
        break;
    case 2: //Cloudy
        sliders->setTemperature( 6000 );
        break;
    case 3: //Thungsten
        sliders->setTemperature( 3200 );
        break;
    case 4: //Fluorescent
        sliders->setTemperature( 4000 );
        break;
    case 5: //Flash
        sliders->setTemperature( 6000 );
        break;
    case 9: //Kelvin
        sliders->setTemperature( getMlvWbKelvin( m_pMlvObject ) );
        break;
    default:
        sliders->setTemperature( 6000 );
        break;
    }
}

//Set the gradient mask into processing module
void MainWindow::setGradientMask(void)
{
    //Send to processing module
    processingSetGradientMask( m_pProcessingObject,
                               getMlvWidth( m_pMlvObject ),
                               getMlvHeight( m_pMlvObject ),
                               (float)m_pGradientElement->getFinalPos().x(),
                               (float)m_pGradientElement->getFinalPos().y(),
                               (float)m_pGradientElement->getStartPos().x(),
                               (float)m_pGradientElement->getStartPos().y() );

    /*qDebug() << "Gradient" << (float)m_pGradientElement->getFinalPos().x() <<
            (float)m_pGradientElement->getFinalPos().y() <<
            (float)m_pGradientElement->getStartPos().x() <<
            (float)m_pGradientElement->getStartPos().y();*/

    m_frameChanged = true;
}

//Calculate correct RAW black level
uint16_t MainWindow::autoCorrectRawBlackLevel()
{
    int factor = 1;
    switch( getMlvBitdepth( m_pMlvObject ) )
    {
        case 10: factor = 16;
            break;
        case 12: factor = 4;
            break;
        default:
            break;
    }
    //If already in range, go with it!
    if( getMlvOriginalBlackLevel( m_pMlvObject ) >= (1700 / factor)
     && getMlvOriginalBlackLevel( m_pMlvObject ) <= (2200 / factor) )
        return getMlvOriginalBlackLevel( m_pMlvObject );

    if( getMlvCameraModel( m_pMlvObject ) == 0x80000218
     || getMlvCameraModel( m_pMlvObject ) == 0x80000261 )
        return 1792 / factor;
    else
        return 2048 / factor;
}

//Get info, if RAW black level is wrong
bool MainWindow::isRawBlackLevelWrong()
{
    int factor = 1;
    switch( getMlvBitdepth( m_pMlvObject ) )
    {
        case 10: factor = 16;
            break;
        case 12: factor = 4;
            break;
        default:
            break;
    }
    //If already in range, go with it!
    if( getMlvOriginalBlackLevel( m_pMlvObject ) >= (1700 / factor)
     && getMlvOriginalBlackLevel( m_pMlvObject ) <= (2200 / factor) )
        return false;
    else
        return true;
}

//Cut In button clicked
void MainWindow::on_toolButtonCutIn_clicked(void)
{
    if( !m_fileLoaded ) return;
    if( ui->horizontalSliderPosition->value() + 1 > ui->spinBoxCutOut->value() )
    {
        QMessageBox::warning( this, tr( "MLV App" ), tr( "Can't set cut in after cut out!" ) );
    }
    else
    {
        ui->spinBoxCutIn->setValue( ui->horizontalSliderPosition->value() + 1 );
    }
}

//Cut Out button clicked
void MainWindow::on_toolButtonCutOut_clicked(void)
{
    if( !m_fileLoaded ) return;
    if( ui->horizontalSliderPosition->value() + 1 < ui->spinBoxCutIn->value() )
    {
        QMessageBox::warning( this, tr( "MLV App" ), tr( "Can't set cut out before cut in!" ) );
    }
    else
    {
        ui->spinBoxCutOut->setValue( ui->horizontalSliderPosition->value() + 1 );
    }
}

//Cut In Delete button clicked
void MainWindow::on_toolButtonCutInDelete_clicked(void)
{
    if( !m_fileLoaded ) return;
    ui->spinBoxCutIn->setValue( 1 );
    ui->spinBoxCutOut->setMinimum( 1 );
}

//Cut Out Delete button clicked
void MainWindow::on_toolButtonCutOutDelete_clicked()
{
    if( !m_fileLoaded ) return;
    ui->spinBoxCutOut->setValue( getMlvFrames( m_pMlvObject ) );
    ui->spinBoxCutIn->setMaximum( getMlvFrames( m_pMlvObject ) );
}

//Cut In Value changed
void MainWindow::on_spinBoxCutIn_valueChanged(int arg1)
{
    ui->spinBoxCutOut->setMinimum( arg1 );

    //Refresh Timecode Label
    if( m_fileLoaded && m_tcModeDuration )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
}

//Cut Out Value changed
void MainWindow::on_spinBoxCutOut_valueChanged(int arg1)
{
    ui->spinBoxCutIn->setMaximum( arg1 );

    //Refresh Timecode Label
    if( m_fileLoaded && m_tcModeDuration )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
}

//Session Preview Disabled
void MainWindow::on_actionPreviewDisabled_triggered()
{
    m_previewMode = 0;
    setPreviewMode();
    addDockWidget( Qt::LeftDockWidgetArea, ui->dockWidgetSession );
    ui->listWidgetSession->verticalScrollBar()->setSingleStep( 1 );
}

//Session Preview  List
void MainWindow::on_actionPreviewList_triggered()
{
    m_previewMode = 1;
    setPreviewMode();
    addDockWidget( Qt::LeftDockWidgetArea, ui->dockWidgetSession );
    ui->listWidgetSession->verticalScrollBar()->setSingleStep( 1 );
}

//Session Preview Picture Left
void MainWindow::on_actionPreviewPicture_triggered()
{
    m_previewMode = 2;
    setPreviewMode();
    addDockWidget( Qt::LeftDockWidgetArea, ui->dockWidgetSession );
    ui->listWidgetSession->verticalScrollBar()->setSingleStep( 82 );
}

//Session Preview Picture Bottom
void MainWindow::on_actionPreviewPictureBottom_triggered()
{
    m_previewMode = 3;
    setPreviewMode();
    addDockWidget( Qt::BottomDockWidgetArea, ui->dockWidgetSession );
    ui->listWidgetSession->verticalScrollBar()->setSingleStep( 82 );
}

//Input of Stretch Width (horizontal) Factor
void MainWindow::on_comboBoxHStretch_currentIndexChanged(int index)
{
    Q_UNUSED( index );
    m_pGradientElement->setStrechFactorX( getHorizontalStretchFactor(false) );
    if( !m_inOpeningProcess && !m_setSliders ) on_horizontalSliderVignetteRadius_valueChanged( ui->horizontalSliderVignetteRadius->value() );
    resultingResolution();
    m_zoomModeChanged = true;
    m_frameChanged = true;
}

//Input of Stretch Height (vertical) Factor
void MainWindow::on_comboBoxVStretch_currentIndexChanged(int index)
{
    Q_UNUSED( index );
    m_pGradientElement->setStrechFactorY( getVerticalStretchFactor(false) );
    if( !m_inOpeningProcess && !m_setSliders ) on_horizontalSliderVignetteRadius_valueChanged( ui->horizontalSliderVignetteRadius->value() );
    resultingResolution();
    m_zoomModeChanged = true;
    m_frameChanged = true;
}

//Timecode label rightclick
void MainWindow::mpTcLabel_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = m_pTcLabel->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionTimecodePositionMiddle );
    myMenu.addAction( ui->actionTimecodePositionRight );
    myMenu.addSeparator();
    myMenu.addAction( ui->actionToggleTimecodeDisplay );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Move Timecode label between icons
void MainWindow::on_actionTimecodePositionMiddle_triggered()
{
    m_timeCodePosition = 1;
    QMessageBox::information( this, QString( "MLV App" ), tr( "Please restart MLV App." ) );
}

//Move Timecode label right
void MainWindow::on_actionTimecodePositionRight_triggered()
{
    m_timeCodePosition = 0;
    QMessageBox::information( this, QString( "MLV App" ), tr( "Please restart MLV App." ) );
}

//TimeCode label doubleclicked
void MainWindow::tcLabelDoubleClicked()
{
    m_tcModeDuration = !m_tcModeDuration;
    m_pTimeCodeImage->setTimeDurationMode( m_tcModeDuration );

    if( !m_fileLoaded )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( 0, 25 ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
        return;
    }

    //Refresh Timecode Label
    if( m_tcModeDuration )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
    else
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->horizontalSliderPosition->value(), getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
}

//TimeCode label, toggle display
void MainWindow::on_actionToggleTimecodeDisplay_triggered()
{
    tcLabelDoubleClicked();
}

//Select Darkframe Subtraction File
void MainWindow::on_toolButtonDarkFrameSubtractionFile_clicked()
{
    QString path = QFileInfo( m_lastDarkframeFileName ).absolutePath();
    if( !QDir( path ).exists() ) path = QDir::homePath();

    //Open File Dialog
    QString fileName = QFileDialog::getOpenFileName( this, tr("Open one or more MLV..."),
                                                    path,
                                                    tr("Magic Lantern Video (*.mlv *.MLV)") );

    if( QFileInfo( fileName ).exists() && fileName.endsWith( ".MLV", Qt::CaseInsensitive ) )
    {
        ui->lineEditDarkFrameFile->setText( fileName );
        m_lastDarkframeFileName = fileName;
    }
}

//Darkframe Subtraction Filename changed
void MainWindow::on_lineEditDarkFrameFile_textChanged(const QString &arg1)
{
    if( QFileInfo( arg1 ).exists() && arg1.endsWith( ".MLV", Qt::CaseInsensitive ) )
    {
#ifdef Q_OS_UNIX
        QByteArray darkFrameFileName = arg1.toUtf8();
#else
        QByteArray darkFrameFileName = arg1.toLatin1();
#endif

        char errorMessage[256] = { 0 };
        int ret = llrpValidateExtDarkFrame(m_pMlvObject, darkFrameFileName.data(), errorMessage);
        if( ret )
        {
            QMessageBox::critical( this, tr( "Error" ), tr( "%1" ).arg( errorMessage ), tr("Cancel") );
            ui->lineEditDarkFrameFile->setText( "No file selected" );
            return;
        }
        else if( !ret && errorMessage[0] )
        {
            QMessageBox::warning( this, tr( "Warning" ), tr( "%1" ).arg( errorMessage ), tr("Ok") );
        }

        llrpInitDarkFrameExtFileName(m_pMlvObject, darkFrameFileName.data());
        ui->toolButtonDarkFrameSubtractionExt->setEnabled( true );
        setToolButtonDarkFrameSubtraction( 1 );
    }
    else
    {
        llrpFreeDarkFrameExtFileName(m_pMlvObject);
        ui->toolButtonDarkFrameSubtractionExt->setEnabled( false );
        setToolButtonDarkFrameSubtraction( 0 );
    }
}

//Check if there is an update availlable
void MainWindow::on_actionCheckForUpdates_triggered( void )
{
    CUpdaterDialog dialog( this, QString( "https://github.com/ilia3101/MLV-App" ), GITVERSION, false );
    dialog.exec();

    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.setValue( "lastUpdateCheck", QDate::currentDate().toString() );
}

//Autocheck for updates told there is an update
void MainWindow::updateCheckResponse(bool arg)
{
    if( arg ) on_actionCheckForUpdates_triggered();

    disconnect( m_pUpdateCheck, SIGNAL(updateAvailable(bool)), this, SLOT(updateCheckResponse(bool)) );
    delete m_pUpdateCheck;

    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.setValue( "lastUpdateCheck", QDate::currentDate().toString() );
}

//Load Lut button pressed
void MainWindow::on_toolButtonLoadLut_clicked()
{
    if( !m_fileLoaded ) return;

    QString path = QFileInfo( m_lastLutFileName ).absolutePath();
    if( !QDir( path ).exists() ) path = QDir::homePath();

    //Open File Dialog
    QString fileName = QFileDialog::getOpenFileName( this, tr("Open cube LUT (*.cube)..."),
                                                    path,
                                                    tr("Cube LUT (*.cube *.CUBE)") );

    if( QFileInfo( fileName ).exists() && fileName.endsWith( ".cube", Qt::CaseInsensitive ) )
    {
        ui->lineEditLutName->setText( fileName );
    }
}

//LUT filename changed
void MainWindow::on_lineEditLutName_textChanged(const QString &arg1)
{
    if( !m_fileLoaded || !m_pProcessingObject ) return;

    if( QFileInfo( arg1 ).exists() && arg1.endsWith( ".cube", Qt::CaseInsensitive ) )
    {
#ifdef Q_OS_UNIX
        QByteArray lutName = arg1.toUtf8();
#else
        QByteArray lutName = arg1.toLatin1();
#endif
        char errorMessage[256] = { 0 };
        int ret = load_lut( m_pProcessingObject->lut, lutName.data(), errorMessage );
        if( ret < 0 )
        {
            QMessageBox::critical( this, tr( "Error" ), tr( "%1" ).arg( errorMessage ), tr("Cancel") );
            ui->lineEditLutName->setText( "" );
            unload_lut( m_pProcessingObject->lut );
            return;
        }
        m_lastLutFileName = arg1;
    }
    else
    {
        unload_lut( m_pProcessingObject->lut );
        ui->lineEditLutName->setText( "" );
    }

    m_frameChanged = true;
}

//Auto correct RAW black level
void MainWindow::on_toolButtonRawBlackAutoCorrect_clicked()
{
    int value = autoCorrectRawBlackLevel();
    if( value != getMlvOriginalBlackLevel( m_pMlvObject ) )
        ui->horizontalSliderRawBlack->setValue( value );
}

//Open UserManualDialog
void MainWindow::on_actionHelp_triggered()
{
    UserManualDialog *help = new UserManualDialog( this );
    help->exec();
    delete help;
}

//"one of the most important features": creating batch MAPP files
void MainWindow::on_actionCreateAllMappFilesNow_triggered()
{
    //Save current clip, to get back to this clip when ready
    int lastClip = m_lastActiveClipInSession;
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
    for( int i = 0; i < ui->listWidgetSession->count(); i++ )
    {
        qApp->processEvents();
        showFileInEditor( i );
        m_pStatusDialog->ui->progressBar->setValue( 100 * i / ui->listWidgetSession->count() );
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

//Show selected file from session in OSX Finder
void MainWindow::on_actionShowInFinder_triggered( void )
{
    if( ui->listWidgetSession->count() == 0 || m_pSessionReceipts.count() == 0 ) return;

    QString path = m_pSessionReceipts.at( ui->listWidgetSession->currentRow() )->fileName();

#ifdef _WIN32    //Code for Windows
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#elif defined(__APPLE__)    //Code for Mac
    QProcess::execute("/usr/bin/osascript", {"-e", "tell application \"Finder\" to reveal POSIX file \"" + path + "\""});
    QProcess::execute("/usr/bin/osascript", {"-e", "tell application \"Finder\" to activate"});
#elif defined( Q_OS_LINUX )
    QProcess::startDetached(QString( "/usr/bin/nautilus \"%1\"" ).arg( QDir::toNativeSeparators(path) ) );
#endif
}

//Show selected file with external application
void MainWindow::on_actionOpenWithExternalApplication_triggered( void )
{
    if( ui->listWidgetSession->count() == 0 || m_pSessionReceipts.count() == 0 ) return;

#ifdef Q_OS_OSX     //Code for OSX
    //First check -> select app if fail
    if( !QDir( m_externalApplicationName ).exists() || m_externalApplicationName.count() == 0 )
    {
        on_actionSelectExternalApplication_triggered();
    }
    //2nd check -> cancel if still fails
    if( !QDir( m_externalApplicationName ).exists() )
    {
        return;
    }
    //Now open
    QFileInfo info( m_externalApplicationName );
    QString path = info.fileName();
    if( path.endsWith( ".app" ) ) path = path.left( path.count() - 4 );
    QProcess::startDetached( QString( "open -a \"%1\" \"%2\"" )
                           .arg( path )
                           .arg( m_pSessionReceipts.at( ui->listWidgetSession->currentRow() )->fileName() ) );
#else    //Code for Windows & Linux
    //First check -> select app if fail
    if( !QFileInfo( m_externalApplicationName ).exists() ) on_actionSelectExternalApplication_triggered();
    //2nd check -> cancel if still fails
    if( !QFileInfo( m_externalApplicationName ).exists() ) return;
    //Now open
    QProcess::execute( QString( "%1" ).arg( m_externalApplicationName ), {QString( "%1" ).arg( QDir::toNativeSeparators( m_pSessionReceipts.at( ui->listWidgetSession->currentRow() )->fileName() ) ) } );
#endif
}

//Select the application for "Open with external application"
void MainWindow::on_actionSelectExternalApplication_triggered()
{
    QString path;
#ifdef _WIN32
    path = "C:\\";
    path = QFileDialog::getOpenFileName( this,
                 tr("Select external application"), path,
                 tr("Executable (*.exe)") );
    if( path.count() == 0 ) return;
#endif
#ifdef Q_OS_LINUX
    path = "/";
    path = QFileDialog::getOpenFileName( this,
                 tr("Select external application"), path,
                 tr("Application (*)") );
    if( path.count() == 0 ) return;
#endif
#ifdef Q_OS_OSX
    path = "/Applications/";
    path = QFileDialog::getOpenFileName( this,
                 tr("Select external application"), path,
                 tr("Application (*.app)") );
    if( path.count() == 0 ) return;
#endif
    m_externalApplicationName = path;
}

//Open one of the recent sessions
void MainWindow::openRecentSession(QString fileName)
{
    //Save actual session?
    if( ui->listWidgetSession->count() > 0 )
    {
        int ret = QMessageBox::warning( this, APPNAME, tr( "Do you like to save the session before loading?" ),
                                                       QMessageBox::Yes, QMessageBox::No, QMessageBox::Cancel );
        //Save
        if( ret == QMessageBox::Yes )
        {
            on_actionSaveSession_triggered();
            //Saving was aborted -> abort quit
            if( m_sessionFileName.count() == 0 )
            {
                return;
            }
        }
        //Cancel
        else if( ret == QMessageBox::Escape || ret == QMessageBox::Cancel )
        {
            return;
        }
    }

    if( !QFileInfo( fileName ).exists() )
    {
        m_pRecentFilesMenu->removeRecentFile( fileName );
        return;
    }

    m_inOpeningProcess = true;
    openSession( fileName );
    //Show last imported file
    showFileInEditor( m_pSessionReceipts.count() - 1 );
    m_sessionFileName = fileName;
    m_lastSessionFileName = fileName;
    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Darktheme standard
void MainWindow::on_actionDarkThemeStandard_triggered(bool checked)
{
    if( checked ) CDarkStyle::assign();
}

//Darktheme by bouncyball
void MainWindow::on_actionDarkThemeModern_triggered(bool checked)
{
    if( checked ) DarkStyleModern::assign();
}

//Debayer algorithm selection per clip
void MainWindow::on_comboBoxDebayer_currentIndexChanged(int index)
{
    Q_UNUSED( index );
    selectDebayerAlgorithm();
}

//Select the debayer algorithm in dependency to playback and chosen playback setting, or clip setting
void MainWindow::selectDebayerAlgorithm()
{
    //Do nothing while preview pics are rendered when importing
    if( m_inOpeningProcess ) return;

    //If no playback active change debayer to receipt settings
    if( !ui->actionPlay->isChecked() || ui->actionDontSwitchDebayerForPlayback->isChecked() )
    {
        switch( ui->comboBoxDebayer->currentIndex() )
        {
        case ReceiptSettings::None:
            setMlvUseNoneDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Simple:
            setMlvUseSimpleDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::Bilinear:
            setMlvDontAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::LMMSE:
            setMlvUseLmmseDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::IGV:
            setMlvUseIgvDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::AMaZE:
            setMlvAlwaysUseAmaze( m_pMlvObject );
            break;
        case ReceiptSettings::AHD:
            setMlvUseAhdDebayer( m_pMlvObject );
            break;
        default:
            break;
        }
        m_pChosenDebayer->setText( ui->comboBoxDebayer->currentText() );
        disableMlvCaching( m_pMlvObject );
    }
    //Else change debayer to the selected one from preview menu
    else
    {
        if( ui->actionUseNoneDebayer->isChecked() )
        {
            setMlvUseNoneDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "None" ) );
        }
        else if( ui->actionUseSimpleDebayer->isChecked() )
        {
            setMlvUseSimpleDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "Simple" ) );
        }
        else if( ui->actionUseBilinear->isChecked() )
        {
            setMlvDontAlwaysUseAmaze( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "Bilinear" ) );
        }
        else if( ui->actionUseLmmseDebayer->isChecked() )
        {
            setMlvUseLmmseDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "LMMSE" ) );
        }
        else if( ui->actionUseIgvDebayer->isChecked() )
        {
            setMlvUseIgvDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "IGV" ) );
        }
        else if( ui->actionUseAhdDebayer->isChecked() )
        {
            setMlvUseAhdDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "AHD" ) );
        }
        else if( ui->actionAlwaysUseAMaZE->isChecked() )
        {
            setMlvAlwaysUseAmaze( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "AMaZE" ) );
        }
        else if( ui->actionCaching->isChecked() )
        {
            setMlvAlwaysUseAmaze( m_pMlvObject );
            enableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "AMaZE" ) );
        }
        ///@todo: ADD HERE OTHER CACHED DEBAYERS! AND ADD SOME SPECIAL TRICK FOR CACHING
    }
    while( !m_pRenderThread->isIdle() ) QThread::msleep(1);
    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    m_frameChanged = true;
}

//Enable/Disable AVIR resizer in viewer
void MainWindow::on_actionBetterResizer_triggered()
{
    m_frameChanged = true;
}

//Show a list of installed fpm files
void MainWindow::on_actionShowInstalledFocusPixelMaps_triggered()
{
    PixelMapListDialog fpmDialog;
    if( m_fileLoaded )
    {
        fpmDialog.showFpm( m_pMlvObject );
    }
    fpmDialog.exec();
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
