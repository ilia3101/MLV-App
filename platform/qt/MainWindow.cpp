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

#include "SystemMemory.h"
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

MainWindow::MainWindow(int &argc, char **argv, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    //Change working directory for C part
    chdir( QCoreApplication::applicationDirPath().toLatin1().data() );

    //Enable color management for macOS
    auto format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    format.setColorSpace( QSurfaceFormat::sRGBColorSpace );
    QSurfaceFormat::setDefaultFormat(format);

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
    m_inClipDeleteProcess = false;

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
    
    resetSliders();

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
            if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            if( SESSION_CLIP_COUNT && askToSaveCurrentSession() ) return;

            m_inOpeningProcess = true;
            openSession( fileName );
            //Show last imported file
            if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );
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
        QTimer::singleShot( 1000, this, SLOT( updateCheck() ) );
    }

    //Temp invisible
    ui->label_GammaText->setVisible( false );
    ui->label_GammaVal->setVisible( false );
    ui->horizontalSliderGamma->setVisible( false );
    //ui->label_Gamut->setVisible( false );
    //ui->comboBoxProcessingGamut->setVisible( false );
    ui->label_TonemappingFunction->setVisible( false );
    ui->comboBoxTonemapFct->setVisible( false );
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
            if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );
            //Caching is in which state? Set it!
            if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            if( SESSION_CLIP_COUNT && askToSaveCurrentSession() ) return false;

            m_inOpeningProcess = true;
            openSession( fileName );
            //Show last imported file
            if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );
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
    if( event->mimeData()->urls().size() > 0 )
    {
        if( event->mimeData()->urls().at(0).path().endsWith( ".MLV", Qt::CaseInsensitive )
         || event->mimeData()->urls().at(0).path().endsWith( ".MCRAW", Qt::CaseInsensitive )
         || event->mimeData()->urls().at(0).path().endsWith( ".FPM", Qt::CaseInsensitive )
         || event->mimeData()->urls().at(0).path().endsWith( ".COMMAND", Qt::CaseInsensitive ) )
        {
            for( int i = 0; i < event->mimeData()->urls().size(); i++ )
            {
                list.append( event->mimeData()->urls().at(i).path() );
            }
            openMlvSet( list );
        }
        else if( event->mimeData()->urls().at(0).path().endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            if( SESSION_CLIP_COUNT && askToSaveCurrentSession() ) return;

            m_inOpeningProcess = true;
            openSession( event->mimeData()->urls().at(0).path() );
            //Show last imported file
            if( SESSION_CLIP_COUNT ) showFileInEditor( SESSION_CLIP_COUNT - 1 );
            m_inOpeningProcess = false;
            m_sessionFileName = event->mimeData()->urls().at(0).path();
            selectDebayerAlgorithm();
        }
    }
    event->acceptProposedAction();
}

//App shall close -> hammer method, we shot on the main class... for making the app close and killing everything in background
void MainWindow::closeEvent(QCloseEvent *event)
{
    ui->actionPlay->setChecked( false );
    on_actionPlay_triggered( false );

    //If user wants to be asked
    if( ui->actionAskForSavingOnQuit->isChecked() && SESSION_CLIP_COUNT != 0 )
    {
        //Ask before quit
        QMessageBox::StandardButton ret = QMessageBox::warning( this, APPNAME, tr( "Do you want to save the current session?" ),
                                                                QMessageBox::Cancel | QMessageBox::Discard | QMessageBox::Save, QMessageBox::Cancel );
        //Aborted
        if( ret == QMessageBox::Escape || ret == QMessageBox::Cancel )
        {
            event->ignore();
            return;
        }
        //Save and quit
        else if( ret == QMessageBox::Save )
        {
            on_actionSaveSession_triggered();
            //Saving was aborted -> abort quit
            if( m_sessionFileName.size() == 0 )
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
            ui->toolButtonBadPixelsSearchMethodEdit->setChecked( false );
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
        else if( !ui->graphicsView->underMouse()
              && !ui->toolButtonBadPixelsSearchMethodEdit->underMouse()
              && !ui->toolButtonBadPixelsCrosshairEnable->underMouse() )
        {
            ui->toolButtonBadPixelsSearchMethodEdit->setChecked( false );
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
    m_previewDebayerGroup->addAction( ui->actionUseRcdDebayer );
    m_previewDebayerGroup->addAction( ui->actionUseDcbDebayer );
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
    m_sessionListGroup->addAction( ui->actionPreviewTableModeBottom );

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
    ui->actionExportReceipt->setEnabled( false );
    //If no clip loaded, disable session save
    ui->actionSaveSession->setEnabled( false );
    ui->actionSaveAsSession->setEnabled( false );
    ui->actionSaveSessionMetadata->setEnabled( false );
    //Set tooltips
    ui->toolButtonCutIn->setToolTip( tr( "Set Cut In    %1" ).arg( ui->toolButtonCutIn->shortcut().toString() ) );
    ui->toolButtonCutOut->setToolTip( tr( "Set Cut Out    %1" ).arg( ui->toolButtonCutOut->shortcut().toString() ) );
    ui->toolButtonBadPixelsSearchMethodEdit->setToolTip( tr( "%1    %2" )
                                                         .arg( ui->toolButtonBadPixelsSearchMethodEdit->toolTip() )
                                                         .arg( ui->toolButtonBadPixelsSearchMethodEdit->shortcut().toString() ) );
    //Set disabled select all and delete clip
    ui->actionDeleteSelectedClips->setEnabled( false );
    ui->actionSelectAllClips->setEnabled( false );
    //disable lut as default
    ui->toolButtonLoadLut->setEnabled( false );
    ui->toolButtonNextLut->setEnabled( false );
    ui->toolButtonPrevLut->setEnabled( false );
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
    ui->labelScope->setScope( NULL, 0, 0, false, false, ScopesLabel::None );
    m_pGraphicsItem = new QGraphicsPixmapItem( QPixmap::fromImage(image) );
    m_pScene = new GraphicsPickerScene( this );
    m_pScene->addItem( m_pGraphicsItem );
    ui->graphicsView->setScene( m_pScene );
    ui->graphicsView->show();
    connect( ui->graphicsView, SIGNAL( customContextMenuRequested(QPoint) ), this, SLOT( pictureCustomContextMenuRequested(QPoint) ) );
    connect( m_pScene, SIGNAL( wbPicked(int,int) ), this, SLOT( whiteBalancePicked(int,int) ) );
    connect( m_pScene, SIGNAL( bpPicked(int,int) ), this, SLOT( badPixelPicked(int,int) ) );
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
    m_pModel = new SessionModel( this );
    m_pProxyModel = new QSortFilterProxyModel( this );
    m_pProxyModel->setSourceModel( m_pModel );
    ui->listViewSession->setModel( m_pProxyModel );
    ui->tableViewSession->setModel( m_pProxyModel );
    ui->tableViewSession->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    ui->tableViewSession->setSortingEnabled( true );
    ui->tableViewSession->sortByColumn(0, Qt::AscendingOrder);
    m_pSelectionModel = ui->listViewSession->selectionModel();

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
    //ui->toolButtonDualIsoForce->setVisible( false );

    //Vidstab
    on_checkBoxVidstabEnable_toggled( false );

    //Sharpen Mask is disabled by default
    ui->label_ShMasking->setEnabled( false );
    ui->label_ShMaskingText->setEnabled( false );
    ui->horizontalSliderShMasking->setEnabled( false );

    //Hide Bad Pixel Map buttons on start
    ui->toolButtonBadPixelsSearchMethodEdit->setVisible( false );
    ui->toolButtonDeleteBpm->setVisible( false );
    ui->toolButtonBadPixelsCrosshairEnable->setVisible( false );

    //Reveal in Explorer
#ifdef Q_OS_WIN
    ui->actionShowInFinder->setText( tr( "Reveal in Explorer" ) );
    ui->actionShowInFinder->setToolTip( tr( "Reveal selected file in Explorer" ) );
#endif
#ifdef Q_OS_LINUX
    ui->actionShowInFinder->setText( tr( "Reveal in file browser" ) );
    ui->actionShowInFinder->setToolTip( tr( "Reveal selected file in file browser" ) );
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

    //Rename selected clip (only one! selected clip)
    connect( ui->actionRename, SIGNAL( triggered() ), this, SLOT( renameActiveClip() ) );
    addAction( ui->actionRename );
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
