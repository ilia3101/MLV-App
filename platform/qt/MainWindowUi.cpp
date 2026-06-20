/*!
 * \file MainWindowUi.cpp
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

//Sets the preview mode
void MainWindow::setPreviewMode( void )
{
    if( m_previewMode == 0 )
    {
        ui->listViewSession->setVisible( true );
        ui->tableViewSession->setVisible( false );
        m_pSelectionModel = ui->listViewSession->selectionModel();
        ui->listViewSession->setViewMode( QListView::ListMode );
        ui->listViewSession->setIconSize( QSize( 0, 0 ) );
        ui->listViewSession->setGridSize( QSize( -1, -1 ) );
        ui->listViewSession->setAlternatingRowColors( true );
        ui->listViewSession->setResizeMode( QListView::Fixed );
        ui->listViewSession->setFlow( QListView::TopToBottom );
        ui->listViewSession->setWrapping( false );
    }
    else if( m_previewMode == 1 )
    {
        ui->listViewSession->setVisible( true );
        ui->tableViewSession->setVisible( false );
        m_pSelectionModel = ui->listViewSession->selectionModel();
        ui->listViewSession->setViewMode( QListView::ListMode );
        ui->listViewSession->setIconSize( QSize( 50, 30 ) );
        ui->listViewSession->setGridSize( QSize( -1, -1 ) );
        ui->listViewSession->setAlternatingRowColors( true );
        ui->listViewSession->setResizeMode( QListView::Fixed );
        ui->listViewSession->setFlow( QListView::TopToBottom );
        ui->listViewSession->setWrapping( false );
    }
    else if( m_previewMode == 2 || m_previewMode == 3 )
    {
        ui->listViewSession->setVisible( true );
        ui->tableViewSession->setVisible( false );
        m_pSelectionModel = ui->listViewSession->selectionModel();
        ui->listViewSession->setViewMode( QListView::IconMode );
        ui->listViewSession->setIconSize( QSize( 130, 80 ) );
        ui->listViewSession->setGridSize( QSize( 140, 100 ) );
        ui->listViewSession->setAlternatingRowColors( false );
        ui->listViewSession->setResizeMode( QListView::Adjust );
        ui->listViewSession->setFlow( QListView::LeftToRight );
        ui->listViewSession->setWrapping( true );
    }
    else //Table mode
    {
        ui->listViewSession->setVisible( false );
        ui->tableViewSession->setVisible( true );
        m_pSelectionModel = ui->tableViewSession->selectionModel();
        ui->listViewSession->setViewMode( QListView::ListMode );
        ui->listViewSession->setIconSize( QSize( 0, 0 ) );
        ui->listViewSession->setGridSize( QSize( -1, -1 ) );
        ui->listViewSession->setAlternatingRowColors( true );
        ui->listViewSession->setResizeMode( QListView::Fixed );
        ui->listViewSession->setFlow( QListView::TopToBottom );
        ui->listViewSession->setWrapping( false );
    }
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
    case 3: ui->toolButtonBadPixelsMap->setChecked( true );
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
    //case 2: ui->toolButtonDualIsoPreview->setChecked( true );
        //break;
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
    //Switch Darkframe Subtraction to OFF if external or internal was selected and no file or data is available
    if( !llrpGetDarkFrameExtStatus( m_pMlvObject ) && index ) index = 0;

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
    else if( ui->toolButtonBadPixelsForce->isChecked() ) return 2;
    else return 3;
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

//Get toolbutton index of dual Iso
int MainWindow::toolButtonDualIsoCurrentIndex()
{
    if( ui->toolButtonDualIsoOff->isChecked() ) return 0;
    return 1;
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
    ui->horizontalSliderPivot->setEnabled( enable );
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
    ui->label_PivotVal->setEnabled( enable );
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
    ui->label_pivot->setEnabled( enable );
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
    if( !SESSION_CLIP_COUNT ) return;
    int x = getMlvWidth( m_pMlvObject ) * getHorizontalStretchFactor( false );
    int y = getMlvHeight( m_pMlvObject ) * getVerticalStretchFactor( false );
    ui->label_resResolution->setText( QString( "%1 x %2 pixels" ).arg(x).arg(y) );
}

//Repaint audio if its size changed
void MainWindow::on_labelAudioTrack_sizeChanged()
{
    paintAudioTrack();
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
    ui->horizontalSliderRawBlack->setMaximum( ( ( 2 << ( getMlvBitdepth( m_pMlvObject ) - 1 ) ) - 1 ) * 10 );
    ui->horizontalSliderRawBlack->blockSignals( false );
    ui->horizontalSliderRawWhite->blockSignals( true );
    ui->horizontalSliderRawWhite->setMaximum( ( 2 << ( getMlvBitdepth( m_pMlvObject ) - 1 ) ) - 1 );
    ui->horizontalSliderRawWhite->setValue( ( 2 << ( getMlvBitdepth( m_pMlvObject ) - 1 ) ) - 1 ); //set value to max, because otherwise the new black value is blocked by old white value
    ui->horizontalSliderRawWhite->blockSignals( false );
    ui->horizontalSliderRawBlack->setValue( getMlvOriginalBlackLevel( m_pMlvObject ) * 10 );
    on_horizontalSliderRawBlack_valueChanged( getMlvOriginalBlackLevel( m_pMlvObject ) * 10 );
    ui->horizontalSliderRawWhite->setValue( getMlvOriginalWhiteLevel( m_pMlvObject ) );
    on_horizontalSliderRawWhite_valueChanged( getMlvOriginalWhiteLevel( m_pMlvObject ) );
}

//Get the current horizontal stretch factor
double MainWindow::getHorizontalStretchFactor( bool downScale )
{
    double factor = 1.0;
    if( ui->comboBoxHStretch->currentIndex() == 0 ) factor = STRETCH_H_100;
    else if( ui->comboBoxHStretch->currentIndex() == 1 ) factor = STRETCH_H_125;
    else if( ui->comboBoxHStretch->currentIndex() == 2 ) factor = STRETCH_H_133;
    else if( ui->comboBoxHStretch->currentIndex() == 3 ) factor = STRETCH_H_150;
    else if( ui->comboBoxHStretch->currentIndex() == 4 ) factor = STRETCH_H_167;
    else if( ui->comboBoxHStretch->currentIndex() == 5 ) factor = STRETCH_H_175;
    else if( ui->comboBoxHStretch->currentIndex() == 6 ) factor = STRETCH_H_180;
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

//Stupid workaround, to make the listViewSession showing clips while importing
void MainWindow::listViewSessionUpdate()
{
    if( !ui->listViewSession->isVisible() ) return;
    ui->listViewSession->setVisible( false );
    ui->listViewSession->update();
    ui->listViewSession->setVisible( true );
}
