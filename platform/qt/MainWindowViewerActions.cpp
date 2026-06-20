/*!
 * \file MainWindowViewerActions.cpp
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

//Show Info Dialog
void MainWindow::on_actionClip_Information_triggered()
{
    if( !m_pInfoDialog->isVisible() ) m_pInfoDialog->show();
    else m_pInfoDialog->hide();
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

//Use RCD debayer
void MainWindow::on_actionUseRcdDebayer_triggered()
{
    selectDebayerAlgorithm();
    return;
}

//Use DCB debayer
void MainWindow::on_actionUseDcbDebayer_triggered()
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

//Session Preview Disabled
void MainWindow::on_actionPreviewDisabled_triggered()
{
    m_previewMode = 0;
    setPreviewMode();
    addDockWidget( Qt::LeftDockWidgetArea, ui->dockWidgetSession );
    ui->listViewSession->verticalScrollBar()->setSingleStep( 1 );
}

//Session Preview  List
void MainWindow::on_actionPreviewList_triggered()
{
    m_previewMode = 1;
    setPreviewMode();
    addDockWidget( Qt::LeftDockWidgetArea, ui->dockWidgetSession );
    ui->listViewSession->verticalScrollBar()->setSingleStep( 1 );
}

//Session Preview Picture Left
void MainWindow::on_actionPreviewPicture_triggered()
{
    m_previewMode = 2;
    setPreviewMode();
    addDockWidget( Qt::LeftDockWidgetArea, ui->dockWidgetSession );
    ui->listViewSession->verticalScrollBar()->setSingleStep( 82 );
}

//Session Preview Picture Bottom
void MainWindow::on_actionPreviewPictureBottom_triggered()
{
    m_previewMode = 3;
    setPreviewMode();
    addDockWidget( Qt::BottomDockWidgetArea, ui->dockWidgetSession );
    ui->listViewSession->verticalScrollBar()->setSingleStep( 82 );
}

//Session Preview Picture Bottom
void MainWindow::on_actionPreviewTableModeBottom_triggered()
{
    m_previewMode = 4;
    setPreviewMode();
    addDockWidget( Qt::BottomDockWidgetArea, ui->dockWidgetSession );
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

//TimeCode label, toggle display
void MainWindow::on_actionToggleTimecodeDisplay_triggered()
{
    tcLabelDoubleClicked();
}

//Enable/Disable AVIR resizer in viewer
void MainWindow::on_actionBetterResizer_triggered()
{
    m_frameChanged = true;
}

//Change viewer background color
void MainWindow::on_actionViewerBackgroundColor_triggered()
{
    QColorDialog *dialog = new QColorDialog( ui->graphicsView->backgroundBrush().color() );
    if( dialog->exec() )
    {
        ui->graphicsView->setBackgroundBrush( QBrush( dialog->selectedColor(), Qt::SolidPattern ) );
    }
    delete dialog;
}
