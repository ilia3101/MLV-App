/*!
 * \file MainWindowPlayback.cpp
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
        //if( cores > 1 ) cores -= 1; // -1 for the processing
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

//Get the framerate. Override or Original
double MainWindow::getFramerate( void )
{
    if( m_fpsOverride ) return m_frameRate;
    else return getMlvFramerate( m_pMlvObject );
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

    // Set sliders after dual ISO processing
    if( toolButtonDualIsoCurrentIndex() == 1 )
    {
        ACTIVE_RECEIPT->setDualIsoAutoCorrected( 1 );

        if( m_pMlvObject->llrawproc->diso_pattern < 0 )
        {
            m_pMlvObject->llrawproc->diso_pattern = -m_pMlvObject->llrawproc->diso_pattern;

            ui->DualIsoPatternComboBox->blockSignals( true );
            ui->DualIsoPatternComboBox->setCurrentIndex(m_pMlvObject->llrawproc->diso_pattern);
            ui->DualIsoPatternComboBox->blockSignals( false );
            //printf("DISO pattern: %d\n", m_pMlvObject->llrawproc->diso_pattern);
        }

        if( m_pMlvObject->llrawproc->diso_auto_correction < 0 )
        {
            if( m_pMlvObject->llrawproc->diso_ev_correction != 1 )
            {
                ui->horizontalSliderDualIsoEvCorrection->blockSignals( true );
                ui->horizontalSliderDualIsoEvCorrection->setValue( (m_pMlvObject->llrawproc->diso_ev_correction * 200) - 0.5 );
                ui->horizontalSliderDualIsoEvCorrection->blockSignals( false );
                ui->DualIsoEvCorrectionVal->setText( QString("%1").arg( m_pMlvObject->llrawproc->diso_ev_correction, 0, 'f', 2 ) );
            }

            if( m_pMlvObject->llrawproc->diso_black_delta != -1 )
            {
                ui->horizontalSliderDualIsoBlackDelta->blockSignals( true );
                ui->horizontalSliderDualIsoBlackDelta->setValue( m_pMlvObject->llrawproc->diso_black_delta );
                ui->horizontalSliderDualIsoBlackDelta->blockSignals( false );
                ui->DualIsoBlackDeltaVal->setText( QString("%1").arg( m_pMlvObject->llrawproc->diso_black_delta ) );
            }

            m_pMlvObject->llrawproc->diso_auto_correction = -m_pMlvObject->llrawproc->diso_auto_correction;

            //printf("DISO: %d, %.2f, %d\n", m_pMlvObject->llrawproc->diso_auto_correction, m_pMlvObject->llrawproc->diso_ev_correction, m_pMlvObject->llrawproc->diso_black_delta);
        }
    }

    //Add zebras on the image
    uint8_t underOver = drawZebras();

    if( ui->actionShowEditArea->isChecked() )
    {
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

    //Bad Pixel crosses in viewer
    if( ui->toolButtonBadPixelsCrosshairEnable->isChecked()
     && toolButtonBadPixelsCurrentIndex() >= 3
     && ui->checkBoxRawFixEnable->isChecked() )
    {
        BadPixelFileHandler::crossesRedrawAll( m_pMlvObject, &m_pBadPixelCrosses, m_pScene );
        BadPixelFileHandler::crossesShowAll( &m_pBadPixelCrosses );
    }
    else
    {
        BadPixelFileHandler::crossesHideAll( &m_pBadPixelCrosses );
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
        case ReceiptSettings::RCD:
            setMlvUseRcdDebayer( m_pMlvObject );
            break;
        case ReceiptSettings::DCB:
            setMlvUseDcbDebayer( m_pMlvObject );
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
        else if( ui->actionUseRcdDebayer->isChecked() )
        {
            setMlvUseRcdDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "RCD" ) );
        }
        else if( ui->actionUseDcbDebayer->isChecked() )
        {
            setMlvUseDcbDebayer( m_pMlvObject );
            disableMlvCaching( m_pMlvObject );
            m_pChosenDebayer->setText( tr( "DCB" ) );
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
