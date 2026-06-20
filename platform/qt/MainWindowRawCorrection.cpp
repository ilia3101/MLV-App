/*!
 * \file MainWindowRawCorrection.cpp
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
    int index = toolButtonBadPixelsCurrentIndex();
    llrpSetBadPixelMode( m_pMlvObject, toolButtonBadPixelsCurrentIndex() );
    ui->toolButtonBadPixelsSearchMethodNormal->setEnabled( ui->checkBoxRawFixEnable->isChecked() );
    ui->toolButtonBadPixelsSearchMethodAggressive->setEnabled( ui->checkBoxRawFixEnable->isChecked() );
    ui->toolButtonBadPixelsSearchMethodEdit->setEnabled( ui->checkBoxRawFixEnable->isChecked() );
    ui->toolButtonDeleteBpm->setEnabled( ui->checkBoxRawFixEnable->isChecked() );
    ui->toolButtonBadPixelsSearchMethodEdit->setVisible( index >= 3 );
    ui->toolButtonDeleteBpm->setVisible( index >= 3 );
    ui->toolButtonBadPixelsCrosshairEnable->setVisible( index >= 3 );
    ui->toolButtonBadPixelsSearchMethodNormal->setVisible( index < 3 );
    ui->toolButtonBadPixelsSearchMethodAggressive->setVisible( index < 3 );
    if( index < 3 ) ui->FocusPixelsInterpolationMethodLabel_2->setText( "Search Method" );
    else ui->FocusPixelsInterpolationMethodLabel_2->setText( "Edit" );

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

//DualISO changed
void MainWindow::toolButtonDualIsoChanged( void )
{
    if( toolButtonDualIsoCurrentIndex() && ui->checkBoxRawFixEnable->isChecked() )
    {
        ui->toolButtonFocusDotInterpolation->setEnabled( false );
        ui->FocusPixelsInterpolationMethodLabel->setEnabled( false );
        ui->toolButtonBadPixelsInterpolation->setEnabled( false );
        ui->BadPixelsInterpolationMethodLabel->setEnabled( false );
        ui->DualIsoPatternLabel->setEnabled( true );
        ui->DualIsoPatternComboBox->setEnabled( true );
        ui->DualIsoMatchExposuresLabel->setEnabled( true );
        ui->toolButtonDualIsoMatchExposures->setEnabled( true );
        ui->DualIsoEvCorrectionLabel->setEnabled( true );
        ui->DualIsoEvCorrectionVal->setEnabled( true );
        ui->horizontalSliderDualIsoEvCorrection->setEnabled( true );
        ui->DualIsoBlackDeltaLabel->setEnabled( true );
        ui->DualIsoBlackDeltaVal->setEnabled( true );
        ui->horizontalSliderDualIsoBlackDelta->setEnabled( true );
        ui->toolButtonDualIsoInterpolation->setEnabled( true );
        ui->toolButtonDualIsoAliasMap->setEnabled( true );
        ui->toolButtonDualIsoFullresBlending->setEnabled( true );
        ui->DualISOInterpolationLabel->setEnabled( true );
        ui->DualISOAliasMapLabel->setEnabled( true );
    }
    else
    {
        ui->toolButtonFocusDotInterpolation->setEnabled( true );
        ui->FocusPixelsInterpolationMethodLabel->setEnabled( true );
        ui->toolButtonBadPixelsInterpolation->setEnabled( true );
        ui->BadPixelsInterpolationMethodLabel->setEnabled( true );
        ui->DualIsoPatternLabel->setEnabled( false );
        ui->DualIsoPatternComboBox->setEnabled( false );
        ui->DualIsoMatchExposuresLabel->setEnabled( false );
        ui->toolButtonDualIsoMatchExposures->setEnabled( false );
        ui->DualIsoEvCorrectionLabel->setEnabled( false );
        ui->DualIsoEvCorrectionVal->setEnabled( false );
        ui->horizontalSliderDualIsoEvCorrection->setEnabled( false );
        ui->DualIsoBlackDeltaLabel->setEnabled( false );
        ui->DualIsoBlackDeltaVal->setEnabled( false );
        ui->horizontalSliderDualIsoBlackDelta->setEnabled( false );
        ui->toolButtonDualIsoInterpolation->setEnabled( false );
        ui->toolButtonDualIsoAliasMap->setEnabled( false );
        ui->toolButtonDualIsoFullresBlending->setEnabled( false );
        ui->DualISOInterpolationLabel->setEnabled( false );
        ui->DualISOAliasMapLabel->setEnabled( false );
    }

    if( !m_fileLoaded ) return;

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

void MainWindow::on_DualIsoPatternComboBox_currentIndexChanged(int index)
{
    if( !m_fileLoaded || m_frameStillDrawing ) return;

    m_pMlvObject->llrawproc->diso_pattern = index;

    if( m_pMlvObject->llrawproc->diso_validity == DISO_FORCED || m_pMlvObject->llrawproc->diso_auto_correction == 2 )
    {
        m_pMlvObject->llrawproc->diso_auto_correction = -2;
        m_pMlvObject->llrawproc->diso_ev_correction = 1;
        m_pMlvObject->llrawproc->diso_black_delta = -1;
    }

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_toolButtonDualIsoMatchExposures1_clicked()
{
    if( !m_fileLoaded || m_frameStillDrawing ) return;

    m_pMlvObject->llrawproc->diso_auto_correction = -1;
    m_pMlvObject->llrawproc->diso_ev_correction = 1;
    m_pMlvObject->llrawproc->diso_black_delta = -1;

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_toolButtonDualIsoMatchExposures2_clicked()
{
    if( !m_fileLoaded || m_frameStillDrawing ) return;

    m_pMlvObject->llrawproc->diso_auto_correction = -2;
    m_pMlvObject->llrawproc->diso_ev_correction = 1;
    m_pMlvObject->llrawproc->diso_black_delta = -1;

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
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

    // Force dual ISO black delta auto correction
    if( m_pMlvObject->llrawproc->diso_auto_correction > 0 )
    {
        m_pMlvObject->llrawproc->diso_auto_correction = -m_pMlvObject->llrawproc->diso_auto_correction;
        m_pMlvObject->llrawproc->diso_black_delta = -1;
    }

    //Force bad pixels and stripes calculation b/c dark frame processing happens before
    llrpResetBpmStatus(m_pMlvObject);
    llrpComputeStripesOn(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
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

//Reset HueVsHue curve with default points
void MainWindow::on_toolButtonHueVsHueResetDefaultPoints_clicked()
{
    ui->labelHueVsHue->resetLineDefaultPoints();
    ui->labelHueVsHue->paintElement();
}

//Reset HueVsSat curve
void MainWindow::on_toolButtonHueVsSatReset_clicked()
{
    ui->labelHueVsSat->resetLine();
    ui->labelHueVsSat->paintElement();
}

//Reset HueVsSat curve with default points
void MainWindow::on_toolButtonHueVsSatResetDefaultPoints_clicked()
{
    ui->labelHueVsSat->resetLineDefaultPoints();
    ui->labelHueVsSat->paintElement();
}

//Reset HueVsLuma curve
void MainWindow::on_toolButtonHueVsLumaReset_clicked()
{
    ui->labelHueVsLuma->resetLine();
    ui->labelHueVsLuma->paintElement();
}

//Reset HueVsLuma curve with default points
void MainWindow::on_toolButtonHueVsLumaResetDefaultPoints_clicked()
{
    ui->labelHueVsLuma->resetLineDefaultPoints();
    ui->labelHueVsLuma->paintElement();
}

//Reset LumaVsSat curve
void MainWindow::on_toolButtonLumaVsSatReset_clicked()
{
    ui->labelLumaVsSat->resetLine();
    ui->labelLumaVsSat->paintElement();
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
    ui->DualISOLabel->setEnabled( checked );
    ui->DualIsoPatternLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualIsoPatternComboBox->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualIsoMatchExposuresLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->toolButtonDualIsoMatchExposures->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualIsoEvCorrectionLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualIsoEvCorrectionVal->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->horizontalSliderDualIsoEvCorrection->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualIsoBlackDeltaLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualIsoBlackDeltaVal->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->horizontalSliderDualIsoBlackDelta->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualISOInterpolationLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualISOAliasMapLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->DualISOFullresBlendingLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->FocusPixelsInterpolationMethodLabel_2->setEnabled( checked );

    ui->toolButtonFocusDots->setEnabled( checked );
    ui->toolButtonFocusDotInterpolation->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() != 1 ) );
    ui->FocusPixelsInterpolationMethodLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() != 1 ) );
    ui->toolButtonBadPixels->setEnabled( checked );
    ui->toolButtonBadPixelsInterpolation->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() != 1 ) );
    ui->BadPixelsInterpolationMethodLabel->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() != 1 ) );
    ui->toolButtonChroma->setEnabled( checked );
    ui->toolButtonPatternNoise->setEnabled( checked );
    ui->toolButtonVerticalStripes->setEnabled( checked );
    ui->toolButtonDualIso->setEnabled( checked );
    ui->toolButtonDualIsoInterpolation->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->toolButtonDualIsoAliasMap->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->toolButtonDualIsoFullresBlending->setEnabled( checked && ( toolButtonDualIsoCurrentIndex() == 1 ) );
    ui->spinBoxDeflickerTarget->setEnabled( checked );
    ui->toolButtonBadPixelsSearchMethodNormal->setEnabled( checked );
    ui->toolButtonBadPixelsSearchMethodAggressive->setEnabled( checked );
    ui->toolButtonBadPixelsSearchMethodEdit->setEnabled( checked );
    ui->toolButtonBadPixelsCrosshairEnable->setEnabled( checked );
    ui->toolButtonDeleteBpm->setEnabled( checked );
    ui->labelDarkFrameSubtraction->setEnabled( checked );
    ui->toolButtonDarkFrameSubtraction->setEnabled( checked );
    ui->toolButtonDarkFrameSubtractionFile->setEnabled( m_fileLoaded && checked );
    ui->lineEditDarkFrameFile->setEnabled( m_fileLoaded && checked );

    ui->toolButtonRawBlackAutoCorrect->setEnabled( isRawBlackLevelWrong() );
    ui->RawBlackLabel->setEnabled( checked );
    ui->horizontalSliderRawBlack->setEnabled( checked );
    ui->label_RawBlackVal->setEnabled( checked );
    ui->RawWhiteLabel->setEnabled( checked );
    ui->horizontalSliderRawWhite->setEnabled( checked );
    ui->label_RawWhiteVal->setEnabled( checked );
    on_horizontalSliderRawBlack_valueChanged( ui->horizontalSliderRawBlack->value() );
    on_horizontalSliderRawWhite_valueChanged( ui->horizontalSliderRawWhite->value() );
}

//Delete the current Bad Pixel Map
void MainWindow::on_toolButtonDeleteBpm_clicked()
{
    if( !m_fileLoaded ) return;
    if( QMessageBox::warning( this, tr( "%1 - Remove bad pixel map" ).arg( APPNAME ), tr( "Delete bad pixel map from disk?" ), tr( "Delete from Disk" ), tr( "Abort" ) ) )
    {
        return;
    }
    if( BadPixelFileHandler::deleteCurrentMap( m_pMlvObject ) )
    {
        QMessageBox::critical( this, tr( "%1 - Delete bad pixel map from disk" ).arg( APPNAME ), tr( "Delete bad pixel map failed!" ) );
        return;
    }
    //Prepare crosses for bad pixel map
    BadPixelFileHandler::crossesPrepareAll( m_pMlvObject, &m_pBadPixelCrosses, m_pScene );
    //Refresh
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

//Activate & Deactivate Bad Pixel Picker
void MainWindow::on_toolButtonBadPixelsSearchMethodEdit_toggled(bool checked)
{
    ui->graphicsView->setBpPickerActive( checked );
    m_pScene->setBpPickerActive( checked );
    m_pGradientElement->setMovable( !checked );

    ui->toolButtonGradientPaint->setChecked( false );
    ui->toolButtonWb->setChecked( false );
    ui->actionWhiteBalancePicker->setChecked( false );
}

//Activate & Deactivate Crosshair for Bad Pixel Picker
void MainWindow::on_toolButtonBadPixelsCrosshairEnable_toggled(bool checked)
{
    if( checked )
    {
        BadPixelFileHandler::crossesRedrawAll( m_pMlvObject, &m_pBadPixelCrosses, m_pScene );
        BadPixelFileHandler::crossesShowAll( &m_pBadPixelCrosses );
    }
    else
    {
        BadPixelFileHandler::crossesHideAll( &m_pBadPixelCrosses );
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
    if( !m_fileLoaded ) return false;

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
            QMessageBox::critical( this, tr( "Error" ), tr( "%1" ).arg( errorMessage ), QMessageBox::Cancel, QMessageBox::Cancel );
            ui->lineEditDarkFrameFile->setText( "No file selected" );
            return;
        }
        else if( !ret && errorMessage[0] )
        {
            QMessageBox::warning( this, tr( "Warning" ), tr( "%1" ).arg( errorMessage ), QMessageBox::Ok , QMessageBox::Ok );
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

//Auto correct RAW black level
void MainWindow::on_toolButtonRawBlackAutoCorrect_clicked()
{
    int value = autoCorrectRawBlackLevel();
    if( value != getMlvOriginalBlackLevel( m_pMlvObject ) )
        ui->horizontalSliderRawBlack->setValue( value * 10 );
}

//Show a list of installed fpm files
void MainWindow::on_actionShowInstalledFocusPixelMaps_triggered()
{
    PixelMapListDialog *fpmDialog = new PixelMapListDialog( this, PixelMapListDialog::FPM );
    if( m_fileLoaded )
    {
        fpmDialog->showCurrentMap( m_pMlvObject );
    }
    fpmDialog->exec();
    delete fpmDialog;
}

//Show a list of installed bpm files
void MainWindow::on_actionShowInstalledBadPixelMaps_triggered()
{
    PixelMapListDialog *bpmDialog = new PixelMapListDialog( this, PixelMapListDialog::BPM );
    if( m_fileLoaded )
    {
        bpmDialog->showCurrentMap( m_pMlvObject );
    }
    bpmDialog->exec();
    delete bpmDialog;
}
