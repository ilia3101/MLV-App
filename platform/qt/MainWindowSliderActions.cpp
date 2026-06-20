/*!
 * \file MainWindowSliderActions.cpp
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

void MainWindow::on_horizontalSliderGamma_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetGamma( m_pProcessingObject, value );
    //processingSetGammaAndTonemapping( m_pProcessingObject, value, processingGetTonemappingFunction( m_pProcessingObject ) );
    ui->label_GammaVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
    ui->lineEditTransferFunction->setText( processingGetTransferFunction( m_pProcessingObject ) );
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

void MainWindow::on_horizontalSliderPivot_valueChanged(int position)
{
    double value = position / 100.0;
    processingSetPivot( m_pProcessingObject, value);
    ui->label_PivotVal->setText( QString("%1").arg( value, 0, 'f', 2 ) );
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

void MainWindow::on_horizontalSliderGrainLumaWeight_valueChanged(int position)
{
    processingSetGrainLumaWeight( m_pProcessingObject, position );
    ui->label_GrainLumaWeight->setText( QString("%1").arg( position ) );
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
    ui->label_RawWhiteVal->setText( QString("%1").arg( position ) );

    if( !m_fileLoaded ) return;
    if( getMlvBitdepth( m_pMlvObject ) == 0 ) return;
    if( getMlvBitdepth( m_pMlvObject ) > 16 ) return;

    if( !ui->checkBoxRawFixEnable->isChecked() )
    {
        position = getMlvOriginalWhiteLevel( m_pMlvObject );
    }
    else if( position <= ui->horizontalSliderRawBlack->value() / 10.0 + 1 )
    {
        position = ui->horizontalSliderRawBlack->value() / 10.0 + 1;
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
    double rawBlack = position / 10.0;
    ui->label_RawBlackVal->setText( QString("%1").arg( rawBlack, 0, 'f', 1 ) );

    if( !m_fileLoaded ) return;
    if( getMlvBitdepth( m_pMlvObject ) == 0 ) return;
    if( getMlvBitdepth( m_pMlvObject ) > 16 ) return;

    if( !ui->checkBoxRawFixEnable->isChecked() )
    {
        rawBlack = getMlvOriginalBlackLevel( m_pMlvObject );
    }
    else if( rawBlack >= ui->horizontalSliderRawWhite->value() - 1 )
    {
        rawBlack = ui->horizontalSliderRawWhite->value() - 1;
        ui->horizontalSliderRawBlack->setValue( rawBlack * 10 );
    }

    while( !m_pRenderThread->isIdle() ) QThread::msleep(1);

    /* Set mlv raw white level to the slider value */
    setMlvBlackLevel( m_pMlvObject, rawBlack );
    /* Set processing white level with correction */
    processingSetBlackLevel( m_pProcessingObject, rawBlack, getMlvBitdepth( m_pMlvObject ) );

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDualIsoEvCorrection_valueChanged(int position)
{
    double ev = position;

    if( position != 1 )
    {
        if( !m_pRenderThread->isIdle() ) return;

        ev /= 200.0;
        ui->DualIsoEvCorrectionVal->setText( QString("%1").arg( ev, 0, 'f', 2 ) );
    }
    else
    {
        if( m_frameStillDrawing ) return;
        m_pMlvObject->llrawproc->diso_auto_correction = -m_pMlvObject->llrawproc->diso_auto_correction;
    }

    if( !m_fileLoaded ) return;

    m_pMlvObject->llrawproc->diso_ev_correction = ev;

    llrpResetFpmStatus(m_pMlvObject);
    llrpResetBpmStatus(m_pMlvObject);
    resetMlvCache( m_pMlvObject );
    resetMlvCachedFrame( m_pMlvObject );
    m_frameChanged = true;
}

void MainWindow::on_horizontalSliderDualIsoBlackDelta_valueChanged(int position)
{
    if( position != -1 )
    {
        if( !m_pRenderThread->isIdle() ) return;
        ui->DualIsoBlackDeltaVal->setText( QString("%1").arg( position ) );
    }
    else
    {
        if( m_frameStillDrawing ) return;
        m_pMlvObject->llrawproc->diso_auto_correction = -m_pMlvObject->llrawproc->diso_auto_correction;
    }

    if( !m_fileLoaded ) return;

    m_pMlvObject->llrawproc->diso_black_delta = position;

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

void MainWindow::on_horizontalSliderPivot_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderPivot->setValue( sliders->pivot() );
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

void MainWindow::on_horizontalSliderGrainLumaWeight_doubleClicked()
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default
    ui->horizontalSliderGrainLumaWeight->setValue( sliders->grainLumaWeight() );
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
    ui->horizontalSliderRawBlack->setValue( getMlvOriginalBlackLevel( m_pMlvObject ) * 10 );
}

void MainWindow::on_horizontalSliderDualIsoEvCorrection_doubleClicked()
{
    on_horizontalSliderDualIsoEvCorrection_valueChanged( 1 );
}

void MainWindow::on_horizontalSliderDualIsoBlackDelta_doubleClicked()
{
    on_horizontalSliderDualIsoBlackDelta_valueChanged( -1 );
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

//DoubleClick on Pivot Label
void MainWindow::on_label_PivotVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderPivot, ui->label_PivotVal, 0.01, 2, 100.0 );
    editSlider.exec();
    ui->horizontalSliderPivot->setValue( editSlider.getValue() );
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

//DoubleClick on GrainLumaWeight Label
void MainWindow::on_label_GrainLumaWeight_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderGrainLumaWeight, ui->label_GrainLumaWeight, 1.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderGrainLumaWeight->setValue( editSlider.getValue() );
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
    editSlider.autoSetup( ui->horizontalSliderRawBlack, ui->label_RawBlackVal, 1.0, 1, 10.0 );
    editSlider.exec();
    ui->horizontalSliderRawBlack->setValue( editSlider.getValue() );
}

void MainWindow::on_DualIsoEvCorrectionVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDualIsoEvCorrection, ui->DualIsoEvCorrectionVal, 0.05, 2, 200.0 );
    editSlider.exec();
    ui->horizontalSliderDualIsoEvCorrection->setValue( editSlider.getValue() - 0.5 );
}

void MainWindow::on_DualIsoBlackDeltaVal_doubleClicked()
{
    EditSliderValueDialog editSlider;
    editSlider.autoSetup( ui->horizontalSliderDualIsoBlackDelta, ui->DualIsoBlackDeltaVal, 5.0, 0, 1.0 );
    editSlider.exec();
    ui->horizontalSliderDualIsoBlackDelta->setValue( editSlider.getValue() );
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
