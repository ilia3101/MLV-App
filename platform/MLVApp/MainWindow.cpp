#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "math.h"

#include <QMessageBox>
#include <QProcess>

#define VERSION "0.1 alpha"

#define MAX_RAM 2048
#define MAX_CORES 2

//Constructor
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_pInfoDialog = new InfoDialog( this );

    ui->comboBox->setVisible( false );

    //Set bools for draw rules
    m_dontDraw = true;
    m_frameStillDrawing = false;
    m_frameChanged = false;
    m_fileLoaded = false;
    m_lastSaveFileName = QString( "/Users/" );

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
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, MAX_RAM );
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

    //m_timerId = startTimer( 16 ); //60fps
    m_timerId = startTimer( 40 ); //25fps
    //m_timerId = startTimer( 1000 ); //1fps
}

//Destructor
MainWindow::~MainWindow()
{
    killTimer( m_timerId );
    delete m_pInfoDialog;
    delete ui;
}

//Timer
void MainWindow::timerEvent(QTimerEvent *t)
{
    if( t->timerId() == m_timerId )
    {
        //Playback
        if( ui->actionPlay->isChecked() )
        {
            //Stop when on last frame
            if( ui->horizontalSliderPosition->value() >= ui->horizontalSliderPosition->maximum() )
            {
                ui->actionPlay->setChecked( false );
            }
            else
            {
                //next frame
                ui->horizontalSliderPosition->setValue( ui->horizontalSliderPosition->value() + 1 );
            }
        }

        if( !m_frameStillDrawing && m_frameChanged && !m_dontDraw )
        {
            m_frameChanged = false; //first do this, if there are changes between rendering
            drawFrame();
        }
        return;
    }
}

//Window resized -> scale picture
void MainWindow::resizeEvent(QResizeEvent *event)
{
    if( m_fileLoaded ) drawFrame();
    event->accept();
}

//Draw a raw picture to the gui
void MainWindow::drawFrame()
{
    m_frameStillDrawing = true;

    //Get frame from library
    getMlvProcessedFrame8( m_pMlvObject, ui->horizontalSliderPosition->value(), m_pRawImage );

    //Some math to have the picture exactly in the frame
    int actWidth = ui->frame->width();
    int desWidth = actWidth;
    int desHeight = actWidth * getMlvHeight(m_pMlvObject) / getMlvWidth(m_pMlvObject);
    int actHeight = ui->frame->height();
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
                                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation) ) );
    m_pRawImageLabel->setMinimumSize( 1, 1 ); //Otherwise window won't be smaller than picture
    m_pRawImageLabel->setAlignment( Qt::AlignCenter ); //Always in the middle

    m_frameStillDrawing = false;
}

//Open MLV
void MainWindow::on_actionOpen_triggered()
{
    //Open File Dialog
    QString fileName = QFileDialog::getOpenFileName( this, tr("Open MLV..."),
                                                    m_lastSaveFileName.left( m_lastSaveFileName.lastIndexOf( "/" ) ),
                                                    tr("Magic Lantern Video (*.mlv)") );

    m_lastSaveFileName = fileName;

    //Exit if not an MLV file or aborted
    if( fileName == QString( "" ) && !fileName.endsWith( ".mlv", Qt::CaseInsensitive ) ) return;

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
    //* Limit frame cache to defined size of RAM */
    setMlvRawCacheLimitMegaBytes( m_pMlvObject, MAX_RAM );
    /* Tell it how many cores we have so it can be optimal */
    setMlvCpuCores( m_pMlvObject, MAX_CORES );

    //Set Clip Info to Dialog
    m_pInfoDialog->ui->tableWidget->item( 0, 1 )->setText( QString( "%1" ).arg( (char*)getMlvCamera( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 1, 1 )->setText( QString( "%1" ).arg( (char*)getMlvLens( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 2, 1 )->setText( QString( "%1 pixel" ).arg( (int)getMlvWidth( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 3, 1 )->setText( QString( "%1 pixel" ).arg( (int)getMlvHeight( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 4, 1 )->setText( QString( "%1" ).arg( (int)getMlvFrames( m_pMlvObject ) ) );
    m_pInfoDialog->ui->tableWidget->item( 5, 1 )->setText( QString( "%1 fps" ).arg( (int)getMlvFramerate( m_pMlvObject ) ) );

    //Adapt slider to clip and move to position 0
    ui->horizontalSliderPosition->setValue( 0 );
    ui->horizontalSliderPosition->setMaximum( getMlvFrames( m_pMlvObject ) - 1 );

    //Restart timer
    m_timerId = startTimer( (int)( 1000.0 / getMlvFramerate( m_pMlvObject ) ) );

    m_fileLoaded = true;

    //enable drawing
    m_dontDraw = false;

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
