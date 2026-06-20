/*!
 * \file MainWindowProcessingControls.cpp
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
    ui->checkBoxExrMode->setEnabled( index > 0 );
    ui->checkBoxExrMode->setChecked( index > 0 );

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

//EXR Mode changed
void MainWindow::on_checkBoxExrMode_toggled(bool checked)
{
    if( !checked )
    {
        processingEnableExr( m_pProcessingObject );
    }
    else
    {
        processingDisableExr( m_pProcessingObject );
    }
    m_frameChanged = true;
}

//AgX checkbox changed
void MainWindow::on_checkBoxAgX_toggled(bool checked)
{
    if( checked )
    {
        processingEnableAgX( m_pProcessingObject );
    }
    else
    {
        processingDisableAgX( m_pProcessingObject );
    }
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

    ui->lineEditTransferFunction->setText( processingGetTransferFunction( m_pProcessingObject ) );
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

//En-/disable all LUT processing
void MainWindow::on_checkBoxLutEnable_clicked(bool checked)
{
    if( checked ) processingEnableLut( m_pProcessingObject );
    else processingDisableLut( m_pProcessingObject );
    m_frameChanged = true;

    ui->toolButtonLoadLut->setEnabled( checked );
    ui->toolButtonNextLut->setEnabled( checked );
    ui->toolButtonPrevLut->setEnabled( checked );
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

//Debayer algorithm selection per clip
void MainWindow::on_comboBoxDebayer_currentIndexChanged(int index)
{
    Q_UNUSED( index );
    selectDebayerAlgorithm();
}

//Changed the transfer function text
void MainWindow::on_lineEditTransferFunction_textChanged(const QString &arg1)
{
#ifdef Q_OS_UNIX
    //qDebug() << "Set Transfer function!" <<
    processingSetTransferFunction( m_pProcessingObject, arg1.toUtf8().data() );
#else
    //qDebug() << "Set Transfer function!" <<
    processingSetTransferFunction( m_pProcessingObject, arg1.toLatin1().data() );
#endif
    m_frameChanged = true;
}
