#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "math.h"

#include <QMessageBox>
#include <QProcess>
#include <QThread>
#include <QTime>

#include "SystemMemory.h"

#define VERSION "0.2 alpha"

//Constructor
MainWindow::MainWindow(int &argc, char **argv, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //Init the Dialogs
    m_pInfoDialog = new InfoDialog( this );
    m_pStatusDialog = new StatusDialog( this );

    //Dont show the Faithful combobox
    ui->comboBox->setVisible( false );
    //Disable unused (for now) actions
    ui->actionCopyRecept->setEnabled( false );
    ui->actionPasteReceipt->setEnabled( false );
    //Disable export until file opened!
    ui->actionExport->setEnabled( false );

    //Set bools for draw rules
    m_dontDraw = true;
    m_frameStillDrawing = false;
    m_frameChanged = false;
    m_fileLoaded = false;

    //Default "last" path
    m_lastSaveFileName = QString( "/Users/" );

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
    m_pFpsStatus->setMaximumWidth( 100 );
    m_pFpsStatus->setMinimumWidth( 100 );
    m_pFpsStatus->setText( tr( "Playback: 0 fps" ) );
    //m_pFpsStatus->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusBar()->addWidget( m_pFpsStatus );

    //m_timerId = startTimer( 16 ); //60fps
    m_timerId = startTimer( 40 ); //25fps
    m_timerCacheId = startTimer( 1000 ); //1fps

    //"Open with" for Windows or scripts
    if( argc > 1 )
    {
        QString fileName = QString( "%1" ).arg( argv[1] );

        //Exit if not an MLV file or aborted
        if( fileName == QString( "" ) && !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) return;

        //Save last file name
        m_lastSaveFileName = fileName;

        //Open the file
        openMlv( fileName );
    }
}

//Destructor
MainWindow::~MainWindow()
{
    killTimer( m_timerId );
    killTimer( m_timerCacheId );
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
    static double newJumpPos;           //Position to jump to, if in Drop Frame Mode

    //Main timer
    if( t->timerId() == m_timerId )
    {
        //Give free one core for responsive GUI
        if( m_frameChanged )
        {
            countTimeDown = 3; //3 secs
            int cores = QThread::idealThreadCount();
            if( cores > 1 ) cores -= 1; // -1 for the processing
            setMlvCpuCores( m_pMlvObject, cores );
        }

        //Playback
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
                }
                //Drop Frame Mode: calc picture for actual time
                else
                {
                    newJumpPos += ((double)getMlvFramerate( m_pMlvObject ) * (double)timeDiff / 1000.0);
                    if( ui->actionLoop->isChecked() && ( newJumpPos > getMlvFrames( m_pMlvObject ) ) )
                    {
                        newJumpPos -= getMlvFrames( m_pMlvObject );
                    }
                    ui->horizontalSliderPosition->setValue( newJumpPos );
                }
            }
        }
        else
        {
            newJumpPos = ui->horizontalSliderPosition->value();
        }

        //Trigger Drawing
        if( !m_frameStillDrawing && m_frameChanged && !m_dontDraw )
        {
            m_frameChanged = false; //first do this, if there are changes between rendering
            drawFrame();

            //Time measurement
            QTime nowTime = QTime::currentTime();
            timeDiff = lastTime.msecsTo( nowTime );
            if( timeDiff != 0 ) m_pFpsStatus->setText( tr( "Playback: %1 fps" ).arg( (int)( 1000 / lastTime.msecsTo( nowTime ) ) ) );
            lastTime = nowTime;

            //When playback is off, the timeDiff is set to 0 for DropFrameMode
            if( !ui->actionPlay->isChecked() ) timeDiff = 1000 / getMlvFramerate( m_pMlvObject );
        }
        else
        {
            m_pFpsStatus->setText( tr( "Playback: 0 fps" ) );
            lastTime = QTime::currentTime(); //do that for calculation of timeDiff;

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
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
        //Exit if not an MLV file or aborted
        QString fileName = openEvent->file();
        if( fileName == QString( "" ) && !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) return false;
        //Save last file name
        m_lastSaveFileName = fileName;
        //Open MLV
        openMlv( fileName );
    }
    return QMainWindow::event(event);
}

//Draw a raw picture to the gui
void MainWindow::drawFrame()
{
    m_frameStillDrawing = true;

    //Get frame from library
    getMlvProcessedFrame8( m_pMlvObject, ui->horizontalSliderPosition->value(), m_pRawImage );

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

    //Bring frame to GUI
    m_pRawImageLabel->setScaledContents(false); //Otherwise Ratio is broken
    m_pRawImageLabel->setPixmap( QPixmap::fromImage( QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                                                         .scaled( desWidth,
                                                                  desHeight,
                                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation) ) ); //alternative: Qt::FastTransformation
    m_pRawImageLabel->setMinimumSize( 1, 1 ); //Otherwise window won't be smaller than picture
    m_pRawImageLabel->setAlignment( Qt::AlignCenter ); //Always in the middle

    m_frameStillDrawing = false;
}

//Open MLV Dialog
void MainWindow::on_actionOpen_triggered()
{
    //Open File Dialog
    QString fileName = QFileDialog::getOpenFileName( this, tr("Open MLV..."),
                                                    m_lastSaveFileName.left( m_lastSaveFileName.lastIndexOf( "/" ) ),
                                                    tr("Magic Lantern Video (*.mlv)") );

    //Exit if not an MLV file or aborted
    if( fileName == QString( "" ) && !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) return;

    //Save last file name
    m_lastSaveFileName = fileName;

    //Open the file
    openMlv( fileName );
}

//Open MLV procedure
void MainWindow::openMlv( QString fileName )
{
    //Set window title to filename
    this->setWindowTitle( QString( "MLV App | %1" ).arg( fileName ) );

    //disable drawing and kill old timer
    killTimer( m_timerId );
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

    m_fileLoaded = true;

    //enable drawing
    m_dontDraw = false;

    //Enable export now
    ui->actionExport->setEnabled( true );

    m_frameChanged = true;
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

//Use AMaZE or not
void MainWindow::on_checkBoxUseAmaze_toggled(bool checked)
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
    qDebug() << "Use AMaZE:" << checked;
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
    ui->label_TintVal->setText( QString("%1").arg( position ) );
    processingSetWhiteBalanceTint( m_pProcessingObject, position );
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
}

//Export clip
//Making a ProRes file:
/*
$ ffmpeg -r 25 -i frame%04d.png -c:v prores_ks -profile:v 4444 output.mov
  ...or...                      -c:v prores_ks -profile:v hq   output.mov
  ...or...                      -c:v prores                    output.mov
*/
void MainWindow::on_actionExport_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString saveFileName = m_lastSaveFileName.left( m_lastSaveFileName.lastIndexOf( "." ) );
    saveFileName.append( ".mov" );

    //File Dialog
    QString fileName = QFileDialog::getSaveFileName( this, tr("Export..."),
                                                    saveFileName,
                                                    tr("Movie (*.mov)") );

    //Exit if not an MOV file or aborted
    if( fileName == QString( "" )
            && ( !fileName.endsWith( ".mov", Qt::CaseInsensitive ) ) ) return;

    //Delete file if exists
    QFile *file = new QFile( fileName );
    if( file->exists() ) file->remove();
    delete file;

    //Disable GUI drawing
    m_dontDraw = true;

    // we always get amaze frames for exporting
    setMlvAlwaysUseAmaze( m_pMlvObject );

    //StatusDialog
    m_pStatusDialog->ui->progressBar->setMaximum( getMlvFrames( m_pMlvObject ) );
    m_pStatusDialog->ui->progressBar->setValue( 0 );
    m_pStatusDialog->show();

    //Create temp pngs
    for( uint32_t i = 0; i < getMlvFrames( m_pMlvObject ); i++ )
    {
        //Append frame number
        QString numberedFileName = fileName.left( fileName.lastIndexOf( "." ) );
        numberedFileName.append( QString( "_%1" ).arg( (uint)i, 5, 10, QChar( '0' ) ) );
        numberedFileName.append( QString( ".png" ) );

        //Get frame from library
        getMlvProcessedFrame8( m_pMlvObject, i, m_pRawImage );

        //Write file
        QImage( ( unsigned char *) m_pRawImage, getMlvWidth(m_pMlvObject), getMlvHeight(m_pMlvObject), QImage::Format_RGB888 )
                    .save( numberedFileName, "png", -1 );

        m_pStatusDialog->ui->progressBar->setValue( i );
        m_pStatusDialog->ui->progressBar->repaint();
        qApp->processEvents();
    }

    //If we don't like amaze we switch it off again
    if( !ui->checkBoxUseAmaze->isChecked() ) setMlvDontAlwaysUseAmaze( m_pMlvObject );

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
    program.append( QString( " -r %1 -i \"%2\" -c:v prores_ks -profile:v 4444 \"%3\"" )
                    .arg( getMlvFramerate( m_pMlvObject ) )
                    .arg( numberedFileName )
                    .arg( output ) );
    qDebug() << program;
    QProcess::execute( program );

    //Update Status
    m_pStatusDialog->ui->progressBar->setValue( m_pStatusDialog->ui->progressBar->maximum() );
    m_pStatusDialog->ui->progressBar->repaint();
    qApp->processEvents();

    //Clean up
    for( uint32_t i = 0; i < getMlvFrames( m_pMlvObject ); i++ )
    {
        //Append frame number
        QString numberedFileName = fileName.left( fileName.lastIndexOf( "." ) );
        numberedFileName.append( QString( "_%1" ).arg( (uint)i, 5, 10, QChar( '0' ) ) );
        numberedFileName.append( QString( ".png" ) );

        //Delete file
        QFile *file = new QFile( numberedFileName );
        if( file->exists() ) file->remove();
        delete file;
    }

    //Hide Status Dialog
    m_pStatusDialog->hide();
}

void MainWindow::on_checkBoxHighLightReconstruction_toggled(bool checked)
{
    if( checked ) processingEnableHighlightReconstruction( m_pProcessingObject );
    else processingDisableHighlightReconstruction( m_pProcessingObject );
    m_frameChanged = true;
}
