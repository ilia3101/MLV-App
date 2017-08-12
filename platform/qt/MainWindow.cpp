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
#include <QMutex>
#include <QXmlStreamWriter>
#include <QDesktopWidget>
#include <png.h>

#include "SystemMemory.h"
#include "ExportSettingsDialog.h"
#include "EditSliderValueDialog.h"

#define VERSION "0.5 alpha"

QMutex gMutex;
QMutex gMutexPng16;
uint32_t gPngThreadsTodo = 0;

//Constructor
MainWindow::MainWindow(int &argc, char **argv, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //Init the GUI
    initGui();

    //Set bools for draw rules
    m_dontDraw = true;
    m_frameStillDrawing = false;
    m_frameChanged = false;
    m_fileLoaded = false;

    //Init the lib
    initLib();

    //Set timers
    m_timerId = startTimer( 40 ); //25fps initially only, is set after import
    m_timerCacheId = startTimer( 1000 ); //1fps

    //Prepare FFmpeg process
    m_pFFmpeg = new QProcess( this );
    connect( m_pFFmpeg, SIGNAL(finished(int)), this, SLOT(endExport()) );
    connect( m_pFFmpeg, SIGNAL(readyReadStandardError()), this, SLOT(readFFmpegOutput()) );
    //Connect Export Handler
    connect( this, SIGNAL(exportReady()), this, SLOT(exportHandler()) );

    //"Open with" for Windows or scripts
    if( argc > 1 )
    {
        QString fileName = QString( "%1" ).arg( argv[1] );

        //Exit if not an MLV file or aborted
        if( QFile(fileName).exists() && fileName.endsWith( ".mlv", Qt::CaseInsensitive ) )
        {
            //File is already opened? Error!
            if( isFileInSession( fileName ) )
            {
                QMessageBox::information( this, tr( "Import MLV" ), tr( "File is already opened in session!" ) );
                return;
            }

            //Save last file name
            m_lastSaveFileName = fileName;

            //Add to SessionList
            addFileToSession( fileName );

            //Open the file
            openMlv( fileName );
            on_actionResetReceipt_triggered();
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            openSession( fileName );
        }
    }
}

//Destructor
MainWindow::~MainWindow()
{
    //Save settings
    writeSettings();
    delete m_pReceiptClipboard;

    disconnect( m_pFFmpeg, SIGNAL(finished(int)), this, SLOT(endExport()) );
    disconnect( m_pFFmpeg, SIGNAL(readyReadStandardError()), this, SLOT(readFFmpegOutput()) );
    delete m_pFFmpeg;

    killTimer( m_timerId );
    killTimer( m_timerCacheId );
    delete m_pHistogram;
    delete m_pStatusDialog;
    delete m_pInfoDialog;
    delete ui;
}

//Timer
void MainWindow::timerEvent(QTimerEvent *t)
{
    static QTime lastTime;              //Last Time a picture was rendered
    static int8_t countTimeDown = -1;   //Time in seconds for CPU countdown
    static int timeDiff = 0;            //TimeDiff between 2 rendered frames in Playback

    //Main timer
    if( t->timerId() == m_timerId )
    {
        //Time measurement
        QTime nowTime = QTime::currentTime();
        timeDiff = lastTime.msecsTo( nowTime );

        //Playback
        playbackHandling( timeDiff );

        //Give free one core for responsive GUI
        if( m_frameChanged )
        {
            countTimeDown = 3; //3 secs
            int cores = QThread::idealThreadCount();
            if( cores > 1 ) cores -= 1; // -1 for the processing
            setMlvCpuCores( m_pMlvObject, cores );
        }

        //Trigger Drawing
        if( !m_frameStillDrawing && m_frameChanged && !m_dontDraw )
        {
            m_frameChanged = false; //first do this, if there are changes between rendering
            drawFrame();

            //fps measurement
            if( timeDiff != 0 ) m_pFpsStatus->setText( tr( "Playback: %1 fps" ).arg( (int)( 1000 / lastTime.msecsTo( nowTime ) ) ) );
            lastTime = nowTime;

            //When playback is off, the timeDiff is set to 0 for DropFrameMode
            if( !ui->actionPlay->isChecked() ) timeDiff = 1000 / getMlvFramerate( m_pMlvObject );
        }
        else
        {
            m_pFpsStatus->setText( tr( "Playback: 0 fps" ) );
            lastTime = QTime::currentTime(); //do that for calculation of timeDiff for DropFrameMode;

        }
        return;
    }
    //1sec Timer
    else if( t->timerId() == m_timerCacheId )
    {
        //Caching Status Label
        if( m_fileLoaded && m_pMlvObject->is_caching )
        {
            m_pCachingStatus->setText( tr( "Caching: active" ) );
        }
        else
        {
            m_pCachingStatus->setText( tr( "Caching: idle" ) );
        }

        //get all cores again
        if( countTimeDown == 0 ) setMlvCpuCores( m_pMlvObject, QThread::idealThreadCount() );
        if( countTimeDown >= 0 ) countTimeDown--;
    }
}

//Window resized -> scale picture
void MainWindow::resizeEvent(QResizeEvent *event)
{
    if( m_fileLoaded ) drawFrame();
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
            //File is already opened? Error!
            if( isFileInSession( fileName ) )
            {
                QMessageBox::information( this, tr( "Import MLV" ), tr( "File is already opened in session!" ) );
            }
            else
            {
                //Save last file name
                m_lastSaveFileName = fileName;
                //Add to SessionList
                addFileToSession( fileName );
                //Open MLV
                openMlv( fileName );
                on_actionResetReceipt_triggered();
            }
        }
        else if( QFile(fileName).exists() && fileName.endsWith( ".masxml", Qt::CaseInsensitive ) )
        {
            openSession( fileName );
        }
        else return false;
    }
    return QMainWindow::event(event);
}

//Draw a raw picture to the gui
void MainWindow::drawFrame( void )
{
    m_frameStillDrawing = true;

    //Get frame from library
    getMlvProcessedFrame8( m_pMlvObject, ui->horizontalSliderPosition->value(), m_pRawImage );

    m_pRawImageLabel->setScaledContents(false); //Otherwise Ratio is broken

    if( ui->actionZoomFit->isChecked() )
    {
        //Some math to have the picture exactly in the frame
        int actWidth = ui->frame->width();
        int actHeight = ui->frame->height();
        int desWidth = actWidth;
        int desHeight = actWidth * getMlvHeight(m_pMlvObject) / getMlvWidth(m_pMlvObject);
        if( desHeight > actHeight )
        {
            desHeight = actHeight;
            desWidth = actHeight * getMlvWidth(m_pMlvObject) / getMlvHeight(m_pMlvObject);
        }

        //Bring frame to GUI (fit to window)
        m_pRawImageLabel->setPixmap( QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                                                             .scaled( desWidth,
                                                                      desHeight,
                                                                      Qt::KeepAspectRatio, Qt::SmoothTransformation) ) ); //alternative: Qt::FastTransformation
    }
    else
    {
        //Bring frame to GUI (100%)
        m_pRawImageLabel->setPixmap( QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                                                             .scaled( getMlvWidth(m_pMlvObject),
                                                                      getMlvHeight(m_pMlvObject),
                                                                      Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation) ) ); //alternative: Qt::FastTransformation
    }
    m_pRawImageLabel->setMinimumSize( 1, 1 ); //Otherwise window won't be smaller than picture
    m_pRawImageLabel->setAlignment( Qt::AlignCenter ); //Always in the middle

    //GetHistogram
    if( ui->actionShowHistogram->isChecked() )
    {
        ui->labelHistogram->setPixmap( QPixmap::fromImage( m_pHistogram->getHistogramFromRaw( m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject) )
                                                          .scaled( 200,
                                                                   70,
                                                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation) ) ); //alternative: Qt::FastTransformation
        ui->labelHistogram->setAlignment( Qt::AlignCenter ); //Always in the middle
    }
    else if( ui->actionShowWaveFormMonitor->isChecked() )
    {
        ui->labelHistogram->setPixmap( QPixmap::fromImage( m_pWaveFormMonitor->getWaveFormMonitorFromRaw( m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject) )
                                                          .scaled( 200,
                                                                   70,
                                                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation) ) ); //alternative: Qt::FastTransformation
        ui->labelHistogram->setAlignment( Qt::AlignCenter ); //Always in the middle
    }
    m_frameStillDrawing = false;
}

//Open MLV Dialog
void MainWindow::on_actionOpen_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //Open File Dialog
    QStringList files = QFileDialog::getOpenFileNames( this, tr("Open one or more MLV..."),
                                                    m_lastSaveFileName.left( m_lastSaveFileName.lastIndexOf( "/" ) ),
                                                    tr("Magic Lantern Video (*.mlv)") );

    if( files.empty() ) return;

    for( int i = 0; i < files.count(); i++ )
    {
        QString fileName = files.at(i);

        //Exit if not an MLV file or aborted
        if( fileName == QString( "" ) && !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) continue;

        //File is already opened? Error!
        if( isFileInSession( fileName ) )
        {
            QMessageBox::information( this, tr( "Import MLV" ), tr( "File %1 is already opened in session!" ).arg( fileName ) );
            continue;
        }

        //Save last file name
        m_lastSaveFileName = fileName;

        //Add file to Sessionlist
        addFileToSession( fileName );

        //Open the file
        openMlv( fileName );
        on_actionResetReceipt_triggered();
    }
}

//Open MLV procedure
void MainWindow::openMlv( QString fileName )
{
    //Set window title to filename
    this->setWindowTitle( QString( "MLV App | %1" ).arg( fileName ) );

    //disable drawing and kill old timer and old WaveFormMonitor
    killTimer( m_timerId );
    delete m_pWaveFormMonitor;
    m_dontDraw = true;

    /* Destroy it just for simplicity... and make a new one */
    freeMlvObject( m_pMlvObject );
    /* Create a NEW object with a NEW MLV clip! */
    m_pMlvObject = initMlvObjectWithClip( fileName.toLatin1().data() );
    /* This funtion really SHOULD be integrated with the one above */
    mapMlvFrames( m_pMlvObject, 0 );
    /* If use has terminal this is useful */
    printMlvInfo( m_pMlvObject );
    /* This needs to be joined (or segmentation fault 11 :D) */
    setMlvProcessing( m_pMlvObject, m_pProcessingObject );
    /* Limit frame cache to defined size of RAM */
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, m_cacheSizeMB );
    /* Tell it how many cores we have so it can be optimal */
    setMlvCpuCores( m_pMlvObject, QThread::idealThreadCount() );

    //Adapt the RawImage to actual size
    int imageSize = getMlvWidth( m_pMlvObject ) * getMlvHeight( m_pMlvObject ) * 3;
    free( m_pRawImage );
    m_pRawImage = ( uint8_t* )malloc( imageSize );

    //Set Clip Info to Dialog
    m_pInfoDialog->ui->tableWidget->item( 0, 1 )->setText( QString( "%1" ).arg( (char*)getMlvCamera( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 1, 1 )->setText( QString( "%1" ).arg( (char*)getMlvLens( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 2, 1 )->setText( QString( "%1 pixel" ).arg( (int)getMlvWidth( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 3, 1 )->setText( QString( "%1 pixel" ).arg( (int)getMlvHeight( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 4, 1 )->setText( QString( "%1" ).arg( (int)getMlvFrames( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 5, 1 )->setText( QString( "%1 fps" ).arg( getMlvFramerate( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 6, 1 )->setText( QString( "%1 µs" ).arg( getMlvShutter( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 7, 1 )->setText( QString( "f %1" ).arg( getMlvAperture( m_pMlvObject ) / 100.0, 0, 'f', 1 ) );
    m_pInfoDialog->ui->tableWidget->item( 8, 1 )->setText( QString( "%1" ).arg( (int)getMlvIso( m_pMlvObject ) ) );

    //Adapt slider to clip and move to position 0
    ui->horizontalSliderPosition->setValue( 0 );
    ui->horizontalSliderPosition->setMaximum( getMlvFrames( m_pMlvObject ) - 1 );

    //Restart timer
    m_timerId = startTimer( (int)( 1000.0 / getMlvFramerate( m_pMlvObject ) ) );

    //Load WaveFormMonitor
    m_pWaveFormMonitor = new WaveFormMonitor( getMlvWidth( m_pMlvObject ) );

    //Always use amaze?
    if( ui->actionAlwaysUseAMaZE->isChecked() ) setMlvAlwaysUseAmaze( m_pMlvObject );
    else setMlvDontAlwaysUseAmaze( m_pMlvObject );

    m_fileLoaded = true;

    //enable drawing
    m_dontDraw = false;

    //Enable export now
    ui->actionExport->setEnabled( true );

    m_frameChanged = true;
}

//Handles the playback and must be triggered from timer
void MainWindow::playbackHandling(int timeDiff)
{
    if( ui->actionPlay->isChecked() )
    {
        //when on last frame
        if( ui->horizontalSliderPosition->value() >= ui->horizontalSliderPosition->maximum() )
        {
            if( ui->actionLoop->isChecked() )
            {
                //Loop, goto first frame
                ui->horizontalSliderPosition->setValue( 0 );
            }
            else
            {
                //Stop
                ui->actionPlay->setChecked( false );
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
                m_newPosDropMode += ((double)getMlvFramerate( m_pMlvObject ) * (double)timeDiff / 1000.0);
                if( ui->actionLoop->isChecked() && ( m_newPosDropMode > getMlvFrames( m_pMlvObject ) ) )
                {
                    m_newPosDropMode -= getMlvFrames( m_pMlvObject );
                }
                ui->horizontalSliderPosition->setValue( m_newPosDropMode );
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

    //Init the Dialogs
    m_pInfoDialog = new InfoDialog( this );
    m_pStatusDialog = new StatusDialog( this );
    m_pHistogram = new Histogram();
    ui->actionShowHistogram->setChecked( true );
    m_pWaveFormMonitor = new WaveFormMonitor( 200 );
    //Fullscreen does not work will, so disable
    ui->actionFullscreen->setVisible( false );
    //Dont show the Faithful combobox
    ui->comboBox->setVisible( false );
    //Disable unused (for now) actions
    ui->actionPasteReceipt->setEnabled( false );
    //Disable export until file opened!
    ui->actionExport->setEnabled( false );

    //Set up image in GUI
    m_pRawImageLabel = new QLabel( this );
    QHBoxLayout* m_layoutFrame;
    m_layoutFrame = new QHBoxLayout( ui->frame );
    m_layoutFrame->addWidget( m_pRawImageLabel );
    m_layoutFrame->setContentsMargins( 0, 0, 0, 0 );

    //Set up caching status label
    m_pCachingStatus = new QLabel( statusBar() );
    m_pCachingStatus->setMaximumWidth( 100 );
    m_pCachingStatus->setMinimumWidth( 100 );
    m_pCachingStatus->setText( tr( "Caching: idle" ) );
    //m_pCachingStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusBar()->addWidget( m_pCachingStatus );

    //Set up fps status label
    m_pFpsStatus = new QLabel( statusBar() );
    m_pFpsStatus->setMaximumWidth( 110 );
    m_pFpsStatus->setMinimumWidth( 110 );
    m_pFpsStatus->setText( tr( "Playback: 0 fps" ) );
    //m_pFpsStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusBar()->addWidget( m_pFpsStatus );

    //Read Settings
    readSettings();

    //Init clipboard
    m_pReceiptClipboard = new ReceiptSettings();

    //Init session settings
    m_pSessionReceipts.clear();

    //Init export Queue
    m_exportQueue.clear();
}

//Initialize the library
void MainWindow::initLib( void )
{
    //Get the amount of RAM
    uint32_t maxRam = getMemorySize() / 1024 / 1024;
    /* Limit frame cache to suitable amount of RAM (~33% at 8GB and below, ~50% at 16GB, then up and up) */
    if (maxRam < 7500) m_cacheSizeMB = maxRam * 0.33;
    else m_cacheSizeMB = (uint32_t)(0.66666f * (float)(maxRam - 4000));
    qDebug() << "Set m_cacheSizeMB to:" << m_cacheSizeMB << "MB of" << maxRam << "MB of total Memory";

    /* Initialise the MLV object so it is actually useful */
    m_pMlvObject = initMlvObject();
    /* Intialise the processing settings object */
    m_pProcessingObject = initProcessingObject();
    /* Set exposure to + 1.2 stops instead of correct 0.0, this is to give the impression
     * (to those that believe) that highlights are recoverable (shhh don't tell) */
    processingSetExposureStops( m_pProcessingObject, 1.2 );
    /* Link video with processing settings */
    setMlvProcessing( m_pMlvObject, m_pProcessingObject );
    /* Limit frame cache to MAX_RAM size */
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, m_cacheSizeMB );
    /* Use AMaZE */
    setMlvDontAlwaysUseAmaze( m_pMlvObject );

    int imageSize = 1856 * 1044 * 3;
    m_pRawImage = ( uint8_t* )malloc( imageSize );
}

//Read some settings from registry
void MainWindow::readSettings()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.beginGroup( "MainWindow" );
    if( !set.value( "size", 0 ).toInt() ) resize( set.value( "size", QSize( 1200, 700 ) ).toSize() );
    if( !set.value( "pos", 0 ).toInt() )
    {
        QPoint point = set.value( "pos", QPoint( 100, 100 ) ).toPoint();
        if( point.x() > QApplication::desktop()->screenGeometry().width() ) point.setX( 100 );
        if( point.y() > QApplication::desktop()->screenGeometry().height() ) point.setY( 100 );
        move( point );
    }
    if( set.value( "maximized", false ).toBool() ) setWindowState( windowState() | Qt::WindowMaximized );
    set.endGroup();
    if( set.value( "dragFrameMode", false ).toBool() ) ui->actionDropFrameMode->setChecked( true );
    if( set.value( "zoomModeFit", true ).toBool() )
    {
        ui->actionZoomFit->setChecked( true );
        ui->actionZoom100->setChecked( false );
    }
    else
    {
        ui->actionZoomFit->setChecked( false );
        ui->actionZoom100->setChecked( true );
    }
    m_lastSaveFileName = set.value( "lastFileName", QString( "/Users/" ) ).toString();
    m_codecProfile = set.value( "codecProfile", 4 ).toUInt();
}

//Save some settings to registry
void MainWindow::writeSettings()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.beginGroup( "MainWindow" );
    set.setValue( "maximized", (bool)( windowState() & Qt::WindowMaximized ) );
    if( !( windowState() & Qt::WindowMaximized ) && !( windowState() & Qt::WindowFullScreen ) )
    {
        set.setValue( "size", size() );
        set.setValue( "pos", pos() );
    }
    set.endGroup();
    set.setValue( "dragFrameMode", ui->actionDropFrameMode->isChecked() );
    set.setValue( "zoomModeFit", ui->actionZoomFit->isChecked() );
    set.setValue( "lastFileName", m_lastSaveFileName );
    set.setValue( "codecProfile", m_codecProfile );
}

//Start exporting a MOV via PNG48
void MainWindow::startExport(QString fileName)
{
    //Delete file if exists
    QFile *file = new QFile( fileName );
    if( file->exists() ) file->remove();
    delete file;

    //Disable GUI drawing
    m_dontDraw = true;

    // we always get amaze frames for exporting
    setMlvAlwaysUseAmaze( m_pMlvObject );

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( getMlvFrames( m_pMlvObject ) * 2 );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->show();

    //Create temp pngs
    gPngThreadsTodo = getMlvFrames( m_pMlvObject );
    QThreadPool *threadPool = new QThreadPool( this );
    //threadPool->setMaxThreadCount( 1 );
    for( uint32_t i = 0; i < getMlvFrames( m_pMlvObject ); i++ )
    {
        //Append frame number
        QString numberedFileName = fileName.left( fileName.lastIndexOf( "." ) );
        numberedFileName.append( QString( "_%1" ).arg( (uint)i, 5, 10, QChar( '0' ) ) );
        numberedFileName.append( QString( ".png" ) );

        RenderPngTask *pngTask = new RenderPngTask( m_pMlvObject, numberedFileName, i );
        threadPool->start( pngTask );
    }

    while( !threadPool->waitForDone(50) )
    {
        gMutex.lock();
        m_pStatusDialog->ui->progressBar->setValue( getMlvFrames( m_pMlvObject ) - gPngThreadsTodo );
        gMutex.unlock();
        m_pStatusDialog->ui->progressBar->repaint();
        qApp->processEvents();
    }
    threadPool->clear();
    delete threadPool;
    qDebug() << "PNGs READY!";

    //Update Progressbar
    m_pStatusDialog->ui->progressBar->setValue( getMlvFrames( m_pMlvObject ) );
    m_pStatusDialog->ui->progressBar->repaint();
    qApp->processEvents();

    //If we don't like amaze we switch it off again
    if( !ui->actionAlwaysUseAMaZE->isChecked() ) setMlvDontAlwaysUseAmaze( m_pMlvObject );

    //Enable GUI drawing
    m_dontDraw = false;

    QString numberedFileName = fileName.left( fileName.lastIndexOf( "." ) );
    QString output = numberedFileName;
    numberedFileName.append( QString( "_\%05d" ) );
    numberedFileName.append( QString( ".png" ) );
    output.append( QString( ".mov" ) );

    QString program = QCoreApplication::applicationDirPath();
    program.append( QString( "/ffmpeg\"" ) );
    program.prepend( QString( "\"" ) );
    program.append( QString( " -r %1 -i \"%2\" -c:v prores_ks -profile:v %3 \"%4\"" )
                    .arg( getMlvFramerate( m_pMlvObject ) )
                    .arg( numberedFileName )
                    .arg( m_codecProfile )
                    .arg( output ) );
    //qDebug() << program;

    //Start FFmpeg
    m_pFFmpeg->start( program );
}

//Adds the fileName to the Session List
void MainWindow::addFileToSession(QString fileName)
{
    //Save settings of actual clip (if there is one)
    if( m_pSessionReceipts.count() > 0 )
    {
        setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );
    }
    //Add to session list
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
}

//Open a session file
void MainWindow::openSession(QString fileName)
{
    QXmlStreamReader Rxml;
    QFile file(fileName);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

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
            //qDebug() << "StartElem";
            while( !Rxml.atEnd() && !Rxml.isEndElement() )
            {
                Rxml.readNext();
                if( Rxml.isStartElement() && Rxml.name() == "clip" )
                {
                    //qDebug() << "Clip!" << Rxml.attributes().at(0).name() << Rxml.attributes().at(0).value();
                    QString fileName = Rxml.attributes().at(0).value().toString();

                    if( QFile( fileName ).exists() )
                    {
                        //Save last file name
                        m_lastSaveFileName = fileName;
                        //Add file to Sessionlist
                        addFileToSession( fileName );
                        //Open the file
                        openMlv( fileName );
                        m_pSessionReceipts.last()->setFileName( fileName );

                        while( !Rxml.atEnd() && !Rxml.isEndElement() )
                        {
                            Rxml.readNext();
                            if( Rxml.isStartElement() && Rxml.name() == "exposure" )
                            {
                                m_pSessionReceipts.last()->setExposure( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "temperature" )
                            {
                                m_pSessionReceipts.last()->setTemperature( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "tint" )
                            {
                                m_pSessionReceipts.last()->setTint( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "saturation" )
                            {
                                m_pSessionReceipts.last()->setSaturation( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "ls" )
                            {
                                m_pSessionReceipts.last()->setLs( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "lr" )
                            {
                                m_pSessionReceipts.last()->setLr( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "ds" )
                            {
                                m_pSessionReceipts.last()->setDs( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "dr" )
                            {
                                m_pSessionReceipts.last()->setDr( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "lightening" )
                            {
                                m_pSessionReceipts.last()->setLightening( Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "highlightReconstruction" )
                            {
                                m_pSessionReceipts.last()->setHighlightReconstruction( (bool)Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() && Rxml.name() == "reinhardTonemapping" )
                            {
                                m_pSessionReceipts.last()->setReinhardTonemapping( (bool)Rxml.readElementText().toInt() );
                                Rxml.readNext();
                            }
                            else if( Rxml.isStartElement() ) //future features
                            {
                                Rxml.readElementText();
                                Rxml.readNext();
                            }
                        }
                    }
                    else
                    {
                        QMessageBox::critical( this, tr( "Open Session Error" ), tr( "File not found: \r\n%1" ).arg( fileName ) );
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
                    setSliders( m_pSessionReceipts.last() );
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
    for( int i = 0; i < ui->listWidgetSession->count(); i++ )
    {
        xmlWriter.writeStartElement( "clip" );
        xmlWriter.writeAttribute( "file", ui->listWidgetSession->item(i)->toolTip() );
        xmlWriter.writeTextElement( "exposure",                QString( "%1" ).arg( m_pSessionReceipts.at(i)->exposure() ) );
        xmlWriter.writeTextElement( "temperature",             QString( "%1" ).arg( m_pSessionReceipts.at(i)->temperature() ) );
        xmlWriter.writeTextElement( "tint",                    QString( "%1" ).arg( m_pSessionReceipts.at(i)->tint() ) );
        xmlWriter.writeTextElement( "saturation",              QString( "%1" ).arg( m_pSessionReceipts.at(i)->saturation() ) );
        xmlWriter.writeTextElement( "ds",                      QString( "%1" ).arg( m_pSessionReceipts.at(i)->ds() ) );
        xmlWriter.writeTextElement( "dr",                      QString( "%1" ).arg( m_pSessionReceipts.at(i)->dr() ) );
        xmlWriter.writeTextElement( "ls",                      QString( "%1" ).arg( m_pSessionReceipts.at(i)->ls() ) );
        xmlWriter.writeTextElement( "lr",                      QString( "%1" ).arg( m_pSessionReceipts.at(i)->lr() ) );
        xmlWriter.writeTextElement( "lightening",              QString( "%1" ).arg( m_pSessionReceipts.at(i)->lightening() ) );
        xmlWriter.writeTextElement( "highlightReconstruction", QString( "%1" ).arg( m_pSessionReceipts.at(i)->isHighlightReconstruction() ) );
        xmlWriter.writeTextElement( "reinhardTonemapping",     QString( "%1" ).arg( m_pSessionReceipts.at(i)->isReinhardTonemapping() ) );
        xmlWriter.writeEndElement();
    }
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();

    file.close();
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
    ui->labelHistogram->setPixmap( QPixmap( ":/IMG/IMG/Histogram.png" ) );
    m_pRawImageLabel->setPixmap( QPixmap( ":/IMG/IMG/Histogram.png" ) );

    //And reset sliders
    on_actionResetReceipt_triggered();

    //Export not possible without mlv file
    ui->actionExport->setEnabled( false );

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

    //Adapt slider to clip and move to position 0
    ui->horizontalSliderPosition->setValue( 0 );
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
void MainWindow::setSliders(ReceiptSettings *receipt)
{
    ui->horizontalSliderExposure->setValue( receipt->exposure() );
    ui->horizontalSliderTemperature->setValue( receipt->temperature() );
    ui->horizontalSliderTint->setValue( receipt->tint() );
    ui->horizontalSliderSaturation->setValue( receipt->saturation() );

    ui->horizontalSliderDS->setValue( receipt->ds() );
    ui->horizontalSliderDR->setValue( receipt->dr() );
    ui->horizontalSliderLS->setValue( receipt->ls() );
    ui->horizontalSliderLR->setValue( receipt->lr() );

    ui->horizontalSliderLighten->setValue( receipt->lightening() );

    ui->checkBoxHighLightReconstruction->setChecked( receipt->isHighlightReconstruction() );
    ui->checkBoxReinhardTonemapping->setChecked( receipt->isReinhardTonemapping() );
}

//Set the receipt from sliders
void MainWindow::setReceipt( ReceiptSettings *receipt )
{
    receipt->setExposure( ui->horizontalSliderExposure->value() );
    receipt->setTemperature( ui->horizontalSliderTemperature->value() );
    receipt->setTint( ui->horizontalSliderTint->value() );
    receipt->setSaturation( ui->horizontalSliderSaturation->value() );
    receipt->setDs( ui->horizontalSliderDS->value() );
    receipt->setDr( ui->horizontalSliderDR->value() );
    receipt->setLs( ui->horizontalSliderLS->value() );
    receipt->setLr( ui->horizontalSliderLR->value() );
    receipt->setLightening( ui->horizontalSliderLighten->value() );
    receipt->setHighlightReconstruction( ui->checkBoxHighLightReconstruction->isChecked() );
    receipt->setReinhardTonemapping( ui->checkBoxReinhardTonemapping->isChecked() );
}

//Show the file in
void MainWindow::showFileInEditor( int row )
{
    //Save slider receipt
    setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );
    //Open new MLV
    openMlv( ui->listWidgetSession->item( row )->toolTip() );
    //Set sliders to receipt
    setSliders( m_pSessionReceipts.at( row ) );
    //Save new position in session
    m_lastActiveClipInSession = row;
    qDebug() << "m_lastActiveClipInSession" << m_lastActiveClipInSession;
}

//Add the clip in SessionList position "row" at last position in ExportQueue
void MainWindow::addClipToExportQueue(int row, QString fileName)
{
    ReceiptSettings *receipt = new ReceiptSettings();
    receipt->setExposure( m_pSessionReceipts.at( row )->exposure() );
    receipt->setTemperature( m_pSessionReceipts.at( row )->temperature() );
    receipt->setTint( m_pSessionReceipts.at( row )->tint() );
    receipt->setSaturation( m_pSessionReceipts.at( row )->saturation() );
    receipt->setDr( m_pSessionReceipts.at( row )->dr() );
    receipt->setDs( m_pSessionReceipts.at( row )->ds() );
    receipt->setLr( m_pSessionReceipts.at( row )->lr() );
    receipt->setLs( m_pSessionReceipts.at( row )->ls() );
    receipt->setLightening( m_pSessionReceipts.at( row )->lightening() );
    receipt->setHighlightReconstruction( m_pSessionReceipts.at( row )->isHighlightReconstruction() );
    receipt->setReinhardTonemapping( m_pSessionReceipts.at( row )->isReinhardTonemapping() );
    receipt->setFileName( m_pSessionReceipts.at( row )->fileName() );
    receipt->setExportFileName( fileName );
    m_exportQueue.append( receipt );
}

//Edit progressbar from FFmpeg output
void MainWindow::readFFmpegOutput( void )
{
    QString output = m_pFFmpeg->readAllStandardError();
    if( !output.startsWith( "frame=" ) ) return;

    //Filter the frame number out and edit progressbar
    output = output.left( output.indexOf("fps=") - 1 ); //Kill everything after frame number
    output = output.right( output.length() - 6 ); //Kill "frame="
    m_pStatusDialog->ui->progressBar->setValue( getMlvFrames( m_pMlvObject ) + output.toUInt() );
}

//Clean up export pngs
void MainWindow::endExport( void )
{
    //Update Status
    m_pStatusDialog->ui->progressBar->setValue( m_pStatusDialog->ui->progressBar->maximum() );
    m_pStatusDialog->ui->progressBar->repaint();
    qApp->processEvents();

    //Clean up
    for( uint32_t i = 0; i < getMlvFrames( m_pMlvObject ); i++ )
    {
        //Append frame number
        QString numberedFileName = m_exportQueue.first()->exportFileName().left( m_exportQueue.first()->exportFileName().lastIndexOf( "." ) );
        numberedFileName.append( QString( "_%1" ).arg( (uint)i, 5, 10, QChar( '0' ) ) );
        numberedFileName.append( QString( ".png" ) );

        //Delete file
        QFile *file = new QFile( numberedFileName );
        if( file->exists() ) file->remove();
        delete file;
    }

    //Emit Ready-Signal
    emit exportReady();
}

//About Window
void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about( this, QString( "About %1" ).arg( "MLV App" ),
                            QString(
                              "<html><img src=':/IMG/IMG/Magic_Lantern_logo_b.png' align='right'/>"
                              "<body><h3>%1</h3>"
                              " <p>%1 v%2</p>"
                             /* " <p>Library v%3.%4 rev%5</p>"*/
                              " <p>%6.</p>"
                              " <p>See <a href='%7'>%7</a> for more information.</p>"
                              " </body></html>" )
                             .arg( "MLV App" )
                             .arg( VERSION )
                             /*.arg( 0 )
                             .arg( 0 )
                             .arg( 0 )*/
                             .arg( "by Ilia3101 & masc" )
                             .arg( "https://github.com/ilia3101/MLV-App" ) );
}

//Position Slider
void MainWindow::on_horizontalSliderPosition_valueChanged(void)
{
    m_frameChanged = true;
}

//Show Info Dialog
void MainWindow::on_actionClip_Information_triggered()
{
    m_pInfoDialog->show();
}

void MainWindow::on_horizontalSliderExposure_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetExposureStops( m_pProcessingObject, value + 1.2 );
    ui->label_ExposureVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderTemperature_valueChanged(int position)
{
    processingSetWhiteBalanceKelvin( m_pProcessingObject, position );
    ui->label_TemperatureVal->setText( QString("%1 K").arg( position ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderTint_valueChanged(int position)
{
    double value = position / 10.0;
    ui->label_TintVal->setText( QString("%1").arg( value, 0, 'f', 1 ) );
    processingSetWhiteBalanceTint( m_pProcessingObject, value );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderSaturation_valueChanged(int position)
{
    double value = pow( position / 100.0 * 2.0, log( 3.6 )/log( 2.0 ) );
    processingSetSaturation( m_pProcessingObject, value );
    ui->label_SaturationVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDS_valueChanged(int position)
{
    double value = position / 10.0;
    processingSetDCFactor( m_pProcessingObject, value );
    ui->label_DsVal->setText( QString("%1").arg( value, 0, 'f', 1 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDR_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetDCRange( m_pProcessingObject, value );
    ui->label_DrVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLS_valueChanged(int position)
{
    double value = position / 10.0;
    processingSetLCFactor( m_pProcessingObject, value );
    ui->label_LsVal->setText( QString("%1").arg( value, 0, 'f', 1 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLR_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetLCRange( m_pProcessingObject, value );
    ui->label_LrVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderLighten_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetLightening( m_pProcessingObject, value );
    ui->label_LightenVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    m_frameChanged = true;
}

//Jump to first frame
void MainWindow::on_actionGoto_First_Frame_triggered()
{
    ui->horizontalSliderPosition->setValue( 0 );
    m_newPosDropMode = 0;
}

//Export clip
void MainWindow::on_actionExport_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //Save slider receipt
    setReceipt( m_pSessionReceipts.at( m_lastActiveClipInSession ) );

    //Filename proposal in dependency to actual file
    QString saveFileName = m_pSessionReceipts.at( m_lastActiveClipInSession )->fileName();
    saveFileName = saveFileName.left( m_lastSaveFileName.lastIndexOf( "." ) );
    saveFileName.append( ".mov" );

    //If one file is selected
    if( ui->listWidgetSession->selectedItems().count() <= 1 )
    {
        //File Dialog
        QString fileName = QFileDialog::getSaveFileName( this, tr("Export..."),
                                                        saveFileName,
                                                        tr("Movie (*.mov)") );

        //Exit if not an MOV file or aborted
        if( fileName == QString( "" )
                && ( !fileName.endsWith( ".mov", Qt::CaseInsensitive ) ) ) return;

        //Get receipt into queue
        addClipToExportQueue( m_lastActiveClipInSession, fileName );
    }
    //if multiple files selected
    else
    {
        qDebug() << "!Multiple Export!";
        //Folder Dialog
        QString folderName = QFileDialog::getExistingDirectory(this, tr("Chose Export Folder"),
                                                          QFileInfo( saveFileName ).absolutePath(),
                                                          QFileDialog::ShowDirsOnly
                                                          | QFileDialog::DontResolveSymlinks);

        if( folderName.length() == 0 ) return;

        //for all selected
        for( int row = 0; row < ui->listWidgetSession->count(); row++ )
        {
            if( !ui->listWidgetSession->item( row )->isSelected() ) continue;

            //Create Path+Name
            QString fileName = ui->listWidgetSession->item( row )->text().replace( ".mlv", ".mov", Qt::CaseInsensitive );
            fileName.prepend( "/" );
            fileName.prepend( folderName );

            //Get receipt into queue
            addClipToExportQueue( row, fileName );
        }
    }
    //Block GUI
    setEnabled( false );
    m_pStatusDialog->setEnabled( true );

    //startExport
    exportHandler();
}

//Enable / Disable the highlight reconstruction
void MainWindow::on_checkBoxHighLightReconstruction_toggled(bool checked)
{
    if( checked ) processingEnableHighlightReconstruction( m_pProcessingObject );
    else processingDisableHighlightReconstruction( m_pProcessingObject );
    m_frameChanged = true;
}

//Enable / Disable Reinhard Tonemapping
void MainWindow::on_checkBoxReinhardTonemapping_toggled(bool checked)
{
    if( checked ) processingEnableTonemapping( m_pProcessingObject );
    else processingDisableTonemapping( m_pProcessingObject );
    m_frameChanged = true;
}

//Click on Zoom: fit
void MainWindow::on_actionZoomFit_triggered()
{
    ui->actionZoomFit->setChecked( true );
    ui->actionZoom100->setChecked( false );
    m_frameChanged = true;
}

//Click on Zoom: 100%
void MainWindow::on_actionZoom100_triggered()
{
    ui->actionZoomFit->setChecked( false );
    ui->actionZoom100->setChecked( true );
    m_frameChanged = true;
}

//Show Histogram or not
void MainWindow::on_actionShowHistogram_triggered(bool checked)
{
    ui->labelHistogram->setVisible( checked );
    if( checked )
    {
        ui->actionShowWaveFormMonitor->setChecked( false );
        m_frameChanged = true;
    }
}

//Show Histogram or not
void MainWindow::on_actionShowWaveFormMonitor_triggered(bool checked)
{
    ui->labelHistogram->setVisible( checked );
    if( checked )
    {
        ui->actionShowHistogram->setChecked( false );
        m_frameChanged = true;
    }
}

//Use AMaZE or not
void MainWindow::on_actionAlwaysUseAMaZE_triggered(bool checked)
{
    if( checked )
    {
        /* Use AMaZE */
        setMlvAlwaysUseAmaze( m_pMlvObject );
    }
    else
    {
        /* Don't use AMaZE */
        setMlvDontAlwaysUseAmaze( m_pMlvObject );
    }
    m_frameChanged = true;
}

//Select the codec
void MainWindow::on_actionExportSettings_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    ExportSettingsDialog *pExportSettings = new ExportSettingsDialog( this, m_codecProfile );
    pExportSettings->exec();
    m_codecProfile = pExportSettings->getEncoderSetting();
    delete pExportSettings;
}

//Reset the edit sliders to default
void MainWindow::on_actionResetReceipt_triggered()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    setSliders( sliders );
    delete sliders;
}

//Copy receipt to clipboard
void MainWindow::on_actionCopyRecept_triggered()
{
    setReceipt( m_pReceiptClipboard );
    ui->actionPasteReceipt->setEnabled( true );
}

//Paste receipt from clipboard
void MainWindow::on_actionPasteReceipt_triggered()
{
    setSliders( m_pReceiptClipboard );
}

//New Session
void MainWindow::on_actionNewSession_triggered()
{
    deleteSession();
    //open empty mlv
    //clear info dialog
    //clear histogram, waveform and picture
}

//Open Session
void MainWindow::on_actionOpenSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSaveFileName ).absolutePath();
    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open MLV App Session Xml"), path,
                                           tr("MLV App Session Xml files (*.masxml)"));

    openSession( fileName );
}

//Save Session
void MainWindow::on_actionSaveSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSaveFileName ).absolutePath();
    QString fileName = QFileDialog::getSaveFileName(this,
                                           tr("Save MLV App Session Xml"), path,
                                           tr("MLV App Session Xml files (*.masxml)"));

    saveSession( fileName );
}

//FileName in SessionList doubleClicked
void MainWindow::on_listWidgetSession_activated(const QModelIndex &index)
{
    showFileInEditor( index.row() );
}

//Sessionlist visibility changed -> redraw picture
void MainWindow::on_dockWidgetSession_visibilityChanged(bool visible)
{
    ui->actionShowSessionArea->setChecked( visible );
    qApp->processEvents();
    m_frameChanged = true;
}

//Edit area visibility changed -> redraw picture
void MainWindow::on_dockWidgetEdit_visibilityChanged(bool visible)
{
    ui->actionShowEditArea->setChecked( visible );
    qApp->processEvents();
    m_frameChanged = true;
}

//Rightclick on SessionList
void MainWindow::on_listWidgetSession_customContextMenuRequested(const QPoint &pos)
{
    if( ui->listWidgetSession->count() <= 0 ) return;
    if( ui->listWidgetSession->selectedItems().size() <= 0 ) return;

    // Handle global position
    QPoint globalPos = ui->listWidgetSession->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    if( ui->listWidgetSession->selectedItems().size() == 1 )
    {
        myMenu.addAction("Show in Editor",  this, SLOT( rightClickShowFile() ) );
        myMenu.addAction("Select all",  this, SLOT( selectAllFiles() ) );
        myMenu.addAction("Delete selected file from session",  this, SLOT( deleteFileFromSession() ) );
    }
    else
    {
        myMenu.addAction("Delete selected files from session",  this, SLOT( deleteFileFromSession() ) );
    }
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Delete selected files from session
void MainWindow::deleteFileFromSession( void )
{
    //If multiple selection is on, we need to erase all selected items
    for( int i = ui->listWidgetSession->selectedItems().size(); i > 0; i-- )
    {
        int row = ui->listWidgetSession->row( ui->listWidgetSession->selectedItems().at( i - 1 ) );
        qDebug() << "Delete row:" << row;
        //Remove item from Session List
        delete ui->listWidgetSession->selectedItems().at( i - 1 );
        //Remove slider memory
        m_pSessionReceipts.removeAt( row );
    }
    //if there is at least one...
    if( ui->listWidgetSession->count() > 0 )
    {
        //Open first!
        ui->listWidgetSession->setCurrentRow( 0 );
        setSliders( m_pSessionReceipts.at( 0 ) );
        openMlv( ui->listWidgetSession->item( 0 )->toolTip() );
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
void MainWindow::on_frame_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->frame->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionZoomFit );
    myMenu.addAction( ui->actionZoom100 );
    if( ui->frame->isFullScreen() )
    {
        myMenu.addSeparator();
        myMenu.addAction( ui->actionGoto_First_Frame );
        myMenu.addAction( ui->actionPlay );
        myMenu.addAction( ui->actionLoop );
        myMenu.addSeparator();
    }
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Contextmenu on scope
void MainWindow::on_labelHistogram_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->labelHistogram->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionShowHistogram );
    myMenu.addAction( ui->actionShowWaveFormMonitor );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//DoubleClick on Exposure Label
void MainWindow::on_label_ExposureVal_doubleClicked( void )
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderExposure, ui->label_ExposureVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderExposure->setValue( editSlider.getValue() );
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
    editSlider.autoSetup( ui->horizontalSliderTint, ui->label_TintVal, 0.1, 1, 10.0 );
    editSlider.exec();
    ui->horizontalSliderTint->setValue( editSlider.getValue() );
}

//DoubleClick on Saturation Label
void MainWindow::on_label_SaturationVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.ui->doubleSpinBox->setMinimum( 0.0 );
    editSlider.ui->doubleSpinBox->setMaximum( 3.6 );
    editSlider.ui->doubleSpinBox->setDecimals( 2 );
    editSlider.ui->doubleSpinBox->setSingleStep( 0.01 );
    editSlider.ui->doubleSpinBox->setValue( ui->label_SaturationVal->text().toDouble() );
    editSlider.ui->doubleSpinBox->selectAll();
    QPoint pos;
    pos.setX(0);
    pos.setY(0);
    pos = ui->label_SaturationVal->mapToGlobal( pos );
    editSlider.setGeometry( pos.x(), pos.y(), 80, 20 );
    editSlider.exec();
    ui->horizontalSliderSaturation->setValue( pow( editSlider.ui->doubleSpinBox->value(), log( 2.0 )/log( 3.6 ) ) * 100.0 / 2.0 );
}

//DoubleClick on Dr Label
void MainWindow::on_label_DrVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDR, ui->label_DrVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderDR->setValue( editSlider.getValue() );
}

//DoubleClick on Ds Label
void MainWindow::on_label_DsVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDS, ui->label_DsVal, 0.1, 1, 10.0 );
    editSlider.exec();
    ui->horizontalSliderDS->setValue( editSlider.getValue() );
}

//DoubleClick on Lr Label
void MainWindow::on_label_LrVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLR, ui->label_LrVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderLR->setValue( editSlider.getValue() );
}

//DoubleClick on Ls Label
void MainWindow::on_label_LsVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLS, ui->label_LsVal, 0.1, 1, 10.0 );
    editSlider.exec();
    ui->horizontalSliderLS->setValue( editSlider.getValue() );
}

//DoubleClick on Lighten Label
void MainWindow::on_label_LightenVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderLighten, ui->label_LightenVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderLighten->setValue( editSlider.getValue() );
}

//Fullscreen Mode
void MainWindow::on_actionFullscreen_triggered( bool checked )
{
    if( checked )
    {
        ui->frame->setWindowFlags( Qt::Dialog );
        ui->frame->showFullScreen();
    }
    else
    {
        ui->frame->setWindowFlags( Qt::Widget );
        ui->frame->showNormal();
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
        jobNumber = 0;
    }
    //Are there jobs?
    if( !m_exportQueue.empty() )
    {
        //Next job!
        exportRunning = true;
        jobNumber++;
        //Open file and settings
        openMlv( m_exportQueue.first()->fileName() );
        //Set sliders to receipt
        setSliders( m_exportQueue.first() );
        //Fill label in StatusDialog
        m_pStatusDialog->ui->label->setText( tr( "%1/%2 - %3" )
                                             .arg( jobNumber )
                                             .arg( numberOfJobs )
                                             .arg( QFileInfo( m_exportQueue.first()->fileName() ).fileName() ) );

        //Start it
        startExport( m_exportQueue.first()->exportFileName() );
        return;
    }
    //Else if all planned exports are ready
    else
    {
        //Hide Status Dialog
        m_pStatusDialog->hide();
        //Open last file which was opened before export
        openMlv( m_pSessionReceipts.at( m_lastActiveClipInSession )->fileName() );
        setSliders( m_pSessionReceipts.at( m_lastActiveClipInSession ) );
        //Unblock GUI
        setEnabled( true );
        //Export is ready
        exportRunning = false;

        QMessageBox::information( this, tr( "Export" ), tr( "Export is ready." ) );
    }
}

//Export a 16bit png frame in a task
void RenderPngTask::run()
{
    png_image image;
    memset( &image, 0, sizeof image );
    image.version = PNG_IMAGE_VERSION;
    image.format = PNG_FORMAT_LINEAR_RGB;
    image.width = getMlvWidth( m_pMlvObject );
    image.height = getMlvHeight( m_pMlvObject );
    png_bytep buffer;
    buffer = (png_bytep)malloc( PNG_IMAGE_SIZE( image ) );

    //Get frame from library
    gMutexPng16.lock();
    getMlvProcessedFrame16( m_pMlvObject, m_frame, (uint16_t*)buffer );
    gMutexPng16.unlock();

    png_image_write_to_file( &image, m_fileName.toLatin1().data(), 0, buffer, 0, NULL );
    free( buffer );
    png_image_free( &image );

    gMutex.lock();
    gPngThreadsTodo--;
    gMutex.unlock();
}
