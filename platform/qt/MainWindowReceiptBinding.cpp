/*!
 * \file MainWindowReceiptBinding.cpp
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

//Reset this receipt with settings from the default receipt
void MainWindow::resetReceiptWithDefault( ReceiptSettings *receipt )
{
    if( !QFileInfo( m_defaultReceiptFileName ).exists() )
    {
        ui->actionUseDefaultReceipt->setChecked( false ); //File doesn't exist, so uncheck the option
        return;
    }

    //Open a XML stream for the file
    QXmlStreamReader Rxml;
    QFile file( m_defaultReceiptFileName );
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

    //Version of settings (values may be interpreted differently)
    int versionReceipt = 0;

    //Parse
    Rxml.setDevice(&file);
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        if( Rxml.isStartElement() && Rxml.name() == QString( "receipt" ) )
        {
            //Read version string, if there is one
            if( Rxml.attributes().size() != 0 )
            {
                //qDebug() << "masxmlVersion" << Rxml.attributes().at(0).value().toInt();
                versionReceipt = Rxml.attributes().at(0).value().toInt();
            }
            readXmlElementsFromFile( &Rxml, receipt, versionReceipt );
        }
    }
    file.close();

    //Never change RAW Black and White Level, reset CutIn/Out
    receipt->setRawWhite( -1 );
    receipt->setRawBlack( -1 );
    receipt->setCutIn( 1 );
    receipt->setCutOut( INT32_MAX );
}

//Imports and sets slider settings from a file to the sliders
void MainWindow::on_actionImportReceipt_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    //If no clip loaded, abort
    if( SESSION_EMPTY ) return;

    QString path = QFileInfo( m_lastReceiptFileName ).absolutePath();
    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open MLV App Receipt Xml"), path,
                                           tr("MLV App Receipt Xml files (*.marxml)"));

    //Abort selected
    if( fileName.size() == 0 ) return;
    m_lastReceiptFileName = fileName;

    //Open a XML stream for the file
    QXmlStreamReader Rxml;
    QFile file(fileName);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

    //Version of settings (values may be interpreted differently)
    int versionReceipt = 0;

    //Parse
    Rxml.setDevice(&file);
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        if( Rxml.isStartElement() && Rxml.name() == QString( "receipt" ) )
        {
            //Read version string, if there is one
            if( Rxml.attributes().size() != 0 )
            {
                //qDebug() << "masxmlVersion" << Rxml.attributes().at(0).value().toInt();
                versionReceipt = Rxml.attributes().at(0).value().toInt();
            }
            readXmlElementsFromFile( &Rxml, m_pReceiptClipboard, versionReceipt );
        }
    }
    file.close();

    m_pCopyMask->exec();
    ui->actionPasteReceipt->setEnabled( true );
    on_actionPasteReceipt_triggered();
}

//Exports the actual slider settings to a file
void MainWindow::on_actionExportReceipt_triggered()
{
    if( SESSION_CLIP_COUNT <= 0 ) return;

    QModelIndexList list = selectedClipsList();
    if( list.size() > 1 ) return;

    int clipToExport;
    if( list.size() == 0 ) clipToExport = SESSION_ACTIVE_CLIP_ROW;
    else clipToExport = m_pProxyModel->mapToSource( list.first() ).row();

    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastReceiptFileName ).absolutePath();
    QString fileName = QFileDialog::getSaveFileName(this,
                                           tr("Save MLV App Receipt Xml"), path,
                                           tr("MLV App Receipt Xml files (*.marxml)"));

    //Abort selected
    if( fileName.size() == 0 ) return;
    if( !fileName.endsWith( ".marxml", Qt::CaseInsensitive ) ) fileName.append( ".marxml" );
    m_lastReceiptFileName = fileName;

    //Save slider receipt
    setReceipt( ACTIVE_RECEIPT );

    QFile file(fileName);
    file.open(QIODevice::WriteOnly);

    //Open a XML writer
    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();

    xmlWriter.writeStartElement( "receipt" );
    xmlWriter.writeAttribute( "version", "4" );
    xmlWriter.writeAttribute( "mlvapp", VERSION );

    writeXmlElementsToFile( &xmlWriter, GET_RECEIPT( clipToExport ) );

    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();

    file.close();
}

//paste the clipboard to the clip in row
void MainWindow::pasteReceiptFromClipboardTo(int row)
{
    //Save current settings into receipt
    if( row == SESSION_ACTIVE_CLIP_ROW )
    {
        setReceipt( GET_RECEIPT(row) );
    }
    //Each selected clip gets the copied receipt
    replaceReceipt( GET_RECEIPT(row), m_pReceiptClipboard, true );
    //If the actual is selected (may have changed since copy action), set sliders and get receipt
    if( row == SESSION_ACTIVE_CLIP_ROW )
    {
        setSliders( GET_RECEIPT(row), true );
    }
}

//Set the edit sliders to settings
void MainWindow::setSliders(ReceiptSettings *receipt, bool paste)
{
    m_setSliders = true;
    ui->horizontalSliderExposure->setValue( receipt->exposure() );
    ui->horizontalSliderContrast->setValue( receipt->contrast() );
    ui->horizontalSliderPivot->setValue( receipt->pivot() );
    if( receipt->temperature() == -1 ) {
        //Init Temp read from the file when imported and loaded very first time completely
        setWhiteBalanceFromMlv( receipt );
    }
    if( receipt->camMatrixUsed() == -1 ) {
        //Init cameramatrix = off for mcraw and else cameramatrix = on
        if( isMcrawLoaded(m_pMlvObject) ) {
            receipt->setCamMatrixUsed(0);
        } else {
            receipt->setCamMatrixUsed(1);
        }
    }
    ui->comboBoxUseCameraMatrix->setCurrentIndex( receipt->camMatrixUsed() );
    on_comboBoxUseCameraMatrix_currentIndexChanged( receipt->camMatrixUsed() );

    ui->horizontalSliderTemperature->setValue( receipt->temperature() );
    on_horizontalSliderTemperature_valueChanged( receipt->temperature() );
    ui->horizontalSliderTint->setValue( receipt->tint() );
    on_horizontalSliderTint_valueChanged( receipt->tint() );

    ui->horizontalSliderClarity->setValue( receipt->clarity() );
    ui->horizontalSliderVibrance->setValue( receipt->vibrance() );
    ui->horizontalSliderSaturation->setValue( receipt->saturation() );

    ui->horizontalSliderDS->setValue( receipt->ds() );
    ui->horizontalSliderDR->setValue( receipt->dr() );
    ui->horizontalSliderLS->setValue( receipt->ls() );
    ui->horizontalSliderLR->setValue( receipt->lr() );

    ui->horizontalSliderLighten->setValue( receipt->lightening() );

    ui->horizontalSliderShadows->setValue( receipt->shadows() );
    ui->horizontalSliderHighlights->setValue( receipt->highlights() );

    ui->labelCurves->setConfiguration( receipt->gradationCurve() );
    ui->labelHueVsHue->setConfiguration( receipt->hueVsHue() );
    ui->labelHueVsSat->setConfiguration( receipt->hueVsSaturation() );
    ui->labelHueVsLuma->setConfiguration( receipt->hueVsLuminance() );
    ui->labelLumaVsSat->setConfiguration( receipt->lumaVsSaturation() );

    ui->checkBoxGradientEnable->setChecked( receipt->isGradientEnabled() );
    ui->horizontalSliderExposureGradient->setValue( receipt->gradientExposure() );
    ui->horizontalSliderContrastGradient->setValue( receipt->gradientContrast() );
    ui->spinBoxGradientX->setValue( receipt->gradientStartX() );
    ui->spinBoxGradientY->setValue( receipt->gradientStartY() );
    ui->dialGradientAngle->setValue( receipt->gradientAngle() );
    ui->spinBoxGradientLength->setValue( receipt->gradientLength() );

    ui->horizontalSliderSharpen->setValue( receipt->sharpen() );
    ui->horizontalSliderShMasking->setValue( receipt->shMasking() );
    ui->horizontalSliderChromaBlur->setValue( receipt->chromaBlur() );

    ui->checkBoxHighLightReconstruction->setChecked( receipt->isHighlightReconstruction() );
    on_checkBoxHighLightReconstruction_toggled( receipt->isHighlightReconstruction() );

    ui->checkBoxChromaSeparation->setChecked( receipt->isChromaSeparation() );
    on_checkBoxChromaSeparation_toggled( receipt->isChromaSeparation() );

    ui->comboBoxProfile->setCurrentIndex( receipt->profile() );
    on_comboBoxProfile_currentIndexChanged( receipt->profile() );
    if( receipt->tonemap() != -1 )
    {
        ui->comboBoxTonemapFct->setCurrentIndex( receipt->tonemap() );
        on_comboBoxTonemapFct_currentIndexChanged( receipt->tonemap() );
    }
    if( receipt->gamut() != -1 )
    {
        ui->comboBoxProcessingGamut->setCurrentIndex( receipt->gamut() );
        on_comboBoxProcessingGamut_currentIndexChanged( receipt->gamut() );
    }
    if( receipt->transferFunction() != QString( "" ) )
    {
        ui->lineEditTransferFunction->setText( receipt->transferFunction() );
    }
    ui->horizontalSliderGamma->setValue( receipt->gamma() );

    ui->checkBoxCreativeAdjustments->setChecked( receipt->allowCreativeAdjustments() );
    on_checkBoxCreativeAdjustments_toggled( receipt->allowCreativeAdjustments() );

    ui->checkBoxExrMode->setChecked(  receipt->exrMode() );
    on_checkBoxExrMode_toggled( receipt->exrMode() );

    ui->checkBoxAgX->setChecked(  receipt->agx() );
    on_checkBoxAgX_toggled( receipt->agx() );

    ui->horizontalSliderDenoiseStrength->setValue( receipt->denoiserStrength() );
    ui->comboBoxDenoiseWindow->setCurrentIndex( receipt->denoiserWindow() - 2 );
    on_comboBoxDenoiseWindow_currentIndexChanged( receipt->denoiserWindow() - 2 );

    ui->horizontalSliderRbfDenoiseLuma->setValue( receipt->rbfDenoiserLuma() );
    ui->horizontalSliderRbfDenoiseChroma->setValue( receipt->rbfDenoiserChroma() );
    ui->horizontalSliderRbfDenoiseRange->setValue( receipt->rbfDenoiserRange() );

    ui->horizontalSliderGrainStrength->setValue( receipt->grainStrength() );
    ui->horizontalSliderGrainLumaWeight->setValue( receipt->grainLumaWeight() );

    ui->checkBoxRawFixEnable->setChecked( receipt->rawFixesEnabled() );
    on_checkBoxRawFixEnable_clicked( receipt->rawFixesEnabled() );
    if( receipt->focusPixels() == -1 )
    {
        //Init Focus Dot automatically when imported and loaded very first time completely
        setToolButtonFocusPixels( llrpDetectFocusDotFixMode( m_pMlvObject ) );
    }
    else
    {
        setToolButtonFocusPixels( receipt->focusPixels() );
    }
    setToolButtonFocusPixelsIntMethod( receipt->fpiMethod() );
    setToolButtonBadPixels( receipt->badPixels() );
    setToolButtonBadPixelsSearchMethod( receipt->bpsMethod() );
    setToolButtonBadPixelsIntMethod( receipt->bpiMethod() );
    setToolButtonChromaSmooth( receipt->chromaSmooth() );
    setToolButtonPatternNoise( receipt->patternNoise() );
    setToolButtonUpsideDown( receipt->upsideDown() );
    if( receipt->verticalStripes() == -1 )
    {
        //Enable by default for 5D3 clips on first load
        if( getMlvCameraModel( m_pMlvObject ) == 0x80000285 ) setToolButtonVerticalStripes( 1 );
        else setToolButtonVerticalStripes( 0 );
    }
    else setToolButtonVerticalStripes( receipt->verticalStripes() );

    //Init
    if( receipt->dualIsoForced() == -1 )
    {
        receipt->setDualIsoForced( llrpGetDualIsoValidity( m_pMlvObject ) );
    }
    //Copy & Paste problems between old and new dual iso
    else if( receipt->dualIsoForced() == DISO_FORCED && llrpGetDualIsoValidity( m_pMlvObject ) == DISO_VALID )
    {
        receipt->setDualIsoForced( DISO_VALID );
    }
    //Copy & Paste problems between old and new dual iso
    else if( receipt->dualIsoForced() == DISO_VALID && llrpGetDualIsoValidity( m_pMlvObject ) != DISO_VALID )
    {
        receipt->setDualIsoForced( DISO_FORCED );
    }
    //ui->toolButtonDualIsoForce->setVisible( receipt->dualIsoForced() != DISO_VALID );
    //ui->toolButtonDualIsoForce->setChecked( receipt->dualIsoForced() == DISO_FORCED );
    //on_toolButtonDualIsoForce_toggled( receipt->dualIsoForced() == DISO_FORCED );

    if( receipt->dualIsoForced() == DISO_FORCED )
    {
        llrpSetDualIsoValidity( m_pMlvObject, 1 );

        ui->toolButtonDualIsoOn->setText(tr( "Force On" ));
        ui->toolButtonDualIsoMatchExposures1->setEnabled( false );
        ui->toolButtonDualIsoMatchExposures2->setChecked( true );
    }
    else
    {
        ui->toolButtonDualIsoOn->setText(tr( "On" ));
        ui->toolButtonDualIsoMatchExposures1->setEnabled( true );
        ui->toolButtonDualIsoMatchExposures1->setChecked( true );
    }

    if( m_pMlvObject->llrawproc->diso_auto_correction > 0 )
    {
        m_pMlvObject->llrawproc->diso_auto_correction = -m_pMlvObject->llrawproc->diso_auto_correction;
    }

    if( !receipt->dualIsoAutoCorrected() )
    {
        receipt->setDualIso( 0 );

        if( receipt->dualIsoForced() == DISO_VALID )
        {
            if( m_pMlvObject->llrawproc->diso1 != m_pMlvObject->llrawproc->diso2 )
            {
                receipt->setDualIso( 1 );
            }

            m_pMlvObject->llrawproc->diso_pattern = 0;
            m_pMlvObject->llrawproc->diso_auto_correction = -1;
            m_pMlvObject->llrawproc->diso_ev_correction = 1;
            m_pMlvObject->llrawproc->diso_black_delta = -1;
        }
        else
        {
            ui->DualIsoPatternComboBox->setCurrentIndex( 0 );
            ui->horizontalSliderDualIsoEvCorrection->setValue( 0 );
            ui->horizontalSliderDualIsoBlackDelta->setValue( 0 );
        }

        if( receipt->dualIsoForced() == DISO_FORCED )
        {
            m_pMlvObject->llrawproc->diso_pattern = 0;
            m_pMlvObject->llrawproc->diso_auto_correction = -2;
            m_pMlvObject->llrawproc->diso_ev_correction = 1;
            m_pMlvObject->llrawproc->diso_black_delta = -1;
        }
    }
    else
    {
        ui->DualIsoPatternComboBox->setCurrentIndex( receipt->dualIsoPattern() );
        on_DualIsoPatternComboBox_currentIndexChanged( receipt->dualIsoPattern() );
        ui->horizontalSliderDualIsoEvCorrection->setValue( receipt->dualIsoEvCorrection() );
        on_horizontalSliderDualIsoEvCorrection_valueChanged( receipt->dualIsoEvCorrection() );
        ui->horizontalSliderDualIsoBlackDelta->setValue( receipt->dualIsoBlackDelta() );
        on_horizontalSliderDualIsoBlackDelta_valueChanged( receipt->dualIsoBlackDelta() );
    }

    setToolButtonDualIso( receipt->dualIso() );
    setToolButtonDualIsoInterpolation( receipt->dualIsoInterpolation() );
    setToolButtonDualIsoAliasMap( receipt->dualIsoAliasMap() );
    setToolButtonDualIsoFullresBlending( receipt->dualIsoFrBlending() );
    ui->spinBoxDeflickerTarget->setValue( receipt->deflickerTarget() );
    on_spinBoxDeflickerTarget_valueChanged( receipt->deflickerTarget() );
    ui->lineEditDarkFrameFile->setText( receipt->darkFrameFileName() );
    on_lineEditDarkFrameFile_textChanged( receipt->darkFrameFileName() );
    ui->toolButtonDarkFrameSubtractionInt->setEnabled( llrpGetDarkFrameIntStatus( m_pMlvObject ) );

    //Auto setup at first full import, else get from receipt
    if( receipt->darkFrameEnabled() == -1 )
    {
        if( llrpGetDarkFrameIntStatus( m_pMlvObject ) )
        {
            setToolButtonDarkFrameSubtraction( 2 );
        }
        else
        {
            setToolButtonDarkFrameSubtraction( 0 );
        }
    }
    else
    {
        setToolButtonDarkFrameSubtraction( receipt->darkFrameEnabled() );
    }

    ui->horizontalSliderTone->setValue( receipt->tone() );
    ui->horizontalSliderToningStrength->setValue( receipt->toningStrength() );

    ui->checkBoxLutEnable->setChecked( receipt->lutEnabled() );
    on_checkBoxLutEnable_clicked( receipt->lutEnabled() );
    ui->lineEditLutName->setText( receipt->lutName() );
    on_lineEditLutName_textChanged( receipt->lutName() );
    ui->horizontalSliderLutStrength->setValue( receipt->lutStrength() );

    ui->checkBoxFilterEnable->setChecked( receipt->filterEnabled() );
    on_checkBoxFilterEnable_clicked( receipt->filterEnabled() );
    ui->comboBoxFilterName->setCurrentIndex( receipt->filterIndex() );
    on_comboBoxFilterName_currentIndexChanged( receipt->filterIndex() );
    ui->horizontalSliderFilterStrength->setValue( receipt->filterStrength() );

    if( receipt->stretchFactorX() == STRETCH_H_100 ) ui->comboBoxHStretch->setCurrentIndex( 0 );
    else if( receipt->stretchFactorX() == STRETCH_H_125 ) ui->comboBoxHStretch->setCurrentIndex( 1 );
    else if( receipt->stretchFactorX() == STRETCH_H_133 ) ui->comboBoxHStretch->setCurrentIndex( 2 );
    else if( receipt->stretchFactorX() == STRETCH_H_150 ) ui->comboBoxHStretch->setCurrentIndex( 3 );
    else if( receipt->stretchFactorX() == STRETCH_H_167 ) ui->comboBoxHStretch->setCurrentIndex( 4 );
    else if( receipt->stretchFactorX() == STRETCH_H_175 ) ui->comboBoxHStretch->setCurrentIndex( 5 );
    else if( receipt->stretchFactorX() == STRETCH_H_180 ) ui->comboBoxHStretch->setCurrentIndex( 6 );
    else ui->comboBoxHStretch->setCurrentIndex( 7 );
    on_comboBoxHStretch_currentIndexChanged( ui->comboBoxHStretch->currentIndex() );

    if( receipt->stretchFactorY() == -1 )
    {
        float ratioV = getMlvAspectRatio( m_pMlvObject );
        if( ratioV == 0.0 ) ratioV = 1.0; // set it to 1 if no information in the MLV file
        //Init vertical stretching automatically when imported and loaded very first time completely
        if( ratioV > 0.9 && ratioV < 1.1 ) ui->comboBoxVStretch->setCurrentIndex( 0 );
        else if( ratioV > 1.6 && ratioV < 1.7 ) ui->comboBoxVStretch->setCurrentIndex( 1 );
        else if( ratioV > 2.9 && ratioV < 3.1 ) ui->comboBoxVStretch->setCurrentIndex( 2 );
        else ui->comboBoxVStretch->setCurrentIndex( 3 );
    }
    else if( receipt->stretchFactorY() == STRETCH_V_100 ) ui->comboBoxVStretch->setCurrentIndex( 0 );
    else if( receipt->stretchFactorY() == STRETCH_V_167 ) ui->comboBoxVStretch->setCurrentIndex( 1 );
    else if( receipt->stretchFactorY() == STRETCH_V_300 ) ui->comboBoxVStretch->setCurrentIndex( 2 );
    else ui->comboBoxVStretch->setCurrentIndex( 3 );
    on_comboBoxVStretch_currentIndexChanged( ui->comboBoxVStretch->currentIndex() );

    //Vignette after stretching in order to use stretching once only
    ui->horizontalSliderVignetteStrength->setValue( receipt->vignetteStrength() );
    ui->horizontalSliderVignetteShape->blockSignals( true );
    ui->horizontalSliderVignetteShape->setValue( receipt->vignetteShape() );
    ui->horizontalSliderVignetteShape->blockSignals( false );
    ui->label_VignetteShapeVal->setText( QString("%1").arg( receipt->vignetteShape() ) ); //Just enter value, rendering through next parameter
    ui->horizontalSliderVignetteRadius->blockSignals( true );
    ui->horizontalSliderVignetteRadius->setValue( receipt->vignetteRadius() );
    ui->horizontalSliderVignetteRadius->blockSignals( false );
    on_horizontalSliderVignetteRadius_valueChanged( receipt->vignetteRadius() );
    ui->horizontalSliderCaRed->setValue( receipt->caRed() );
    on_horizontalSliderCaRed_valueChanged( receipt->caRed() );
    ui->horizontalSliderCaBlue->setValue( receipt->caBlue() );
    on_horizontalSliderCaBlue_valueChanged( receipt->caBlue() );
    ui->horizontalSliderCaDesaturate->setValue( receipt->caDesaturate() );
    on_horizontalSliderCaDesaturate_valueChanged( receipt->caDesaturate() );
    ui->horizontalSliderCaRadius->setValue( receipt->caRadius() );
    on_horizontalSliderCaRadius_valueChanged( receipt->caRadius() );

    if( !paste && !receipt->wasNeverLoaded() )
    {
        ui->spinBoxCutIn->setValue( receipt->cutIn() );
        on_spinBoxCutIn_valueChanged( receipt->cutIn() );
        ui->spinBoxCutOut->setValue( receipt->cutOut() );
        on_spinBoxCutOut_valueChanged( receipt->cutOut() );
    }

    if( receipt->rawBlack() != -1 )
    {
        ui->horizontalSliderRawBlack->setValue( receipt->rawBlack() );
    }
    if( receipt->rawWhite() != -1 )
    {
        ui->horizontalSliderRawWhite->setValue( receipt->rawWhite() );
    }

    m_pMlvObject->current_cached_frame_active = 0;

    if( ui->actionPlaybackPosition->isChecked() ) ui->horizontalSliderPosition->setValue( receipt->lastPlaybackPosition() );
    ui->comboBoxDebayer->setCurrentIndex( receipt->debayer() );
    on_comboBoxDebayer_currentIndexChanged( receipt->debayer() );

    ui->checkBoxVidstabEnable->setChecked( receipt->vidStabEnabled() );
    on_checkBoxVidstabEnable_toggled( receipt->vidStabEnabled() );
    ui->horizontalSliderVidstabStepsize->setValue( receipt->vidStabStepsize() );
    ui->horizontalSliderVidstabShakiness->setValue( receipt->vidStabShakiness() );
    ui->horizontalSliderVidstabAccuracy->setValue( receipt->vidStabAccuracy() );
    ui->horizontalSliderVidstabZoom->setValue( receipt->vidStabZoom() );
    ui->horizontalSliderVidstabSmoothing->setValue( receipt->vidStabSmoothing() );
    ui->checkBoxVidstabTripod->setChecked( receipt->vidStabTripod() );
    on_checkBoxVidstabTripod_toggled( receipt->vidStabTripod() );

    m_setSliders = false;
}

void MainWindow::resetSliders( void )
{
    ReceiptSettings *sliders = new ReceiptSettings(); //default

    if( ui->actionUseDefaultReceipt->isChecked() ) resetReceiptWithDefault( sliders );

    sliders->setRawWhite( 0 );
    sliders->setRawBlack( 0 );

    sliders->setDualIsoForced( 0 );
    sliders->setDualIso( 0 );

    setSliders( sliders, false );

    setToolButtonDarkFrameSubtraction( 0 );
    ui->lineEditDarkFrameFile->setEnabled( false );
    ui->toolButtonDarkFrameSubtractionFile->setEnabled( false );
    ui->toolButtonDarkFrameSubtractionInt->setEnabled( false );

    setToolButtonFocusPixels( 0 );
    setToolButtonBadPixels( 0 );

    delete sliders;
}

//Set the receipt from sliders
void MainWindow::setReceipt( ReceiptSettings *receipt )
{
    receipt->setExposure( ui->horizontalSliderExposure->value() );
    receipt->setContrast( ui->horizontalSliderContrast->value() );
    receipt->setPivot( ui->horizontalSliderPivot->value() );
    receipt->setTemperature( ui->horizontalSliderTemperature->value() );
    receipt->setTint( ui->horizontalSliderTint->value() );
    receipt->setClarity( ui->horizontalSliderClarity->value() );
    receipt->setVibrance( ui->horizontalSliderVibrance->value() );
    receipt->setSaturation( ui->horizontalSliderSaturation->value() );
    receipt->setDs( ui->horizontalSliderDS->value() );
    receipt->setDr( ui->horizontalSliderDR->value() );
    receipt->setLs( ui->horizontalSliderLS->value() );
    receipt->setLr( ui->horizontalSliderLR->value() );
    receipt->setLightening( ui->horizontalSliderLighten->value() );
    receipt->setShadows( ui->horizontalSliderShadows->value() );
    receipt->setHighlights( ui->horizontalSliderHighlights->value() );
    receipt->setGradationCurve( ui->labelCurves->configuration() );
    receipt->setHueVsHue( ui->labelHueVsHue->configuration() );
    receipt->setHueVsSaturation( ui->labelHueVsSat->configuration() );
    receipt->setHueVsLuminance( ui->labelHueVsLuma->configuration() );
    receipt->setLumaVsSaturation( ui->labelLumaVsSat->configuration() );

    receipt->setGradientEnabled( ui->checkBoxGradientEnable->isChecked() );
    receipt->setGradientExposure( ui->horizontalSliderExposureGradient->value() );
    receipt->setGradientContrast( ui->horizontalSliderContrastGradient->value() );
    receipt->setGradientStartX( ui->spinBoxGradientX->value() );
    receipt->setGradientStartY( ui->spinBoxGradientY->value() );
    receipt->setGradientLength( ui->spinBoxGradientLength->value() );
    receipt->setGradientAngle( ui->dialGradientAngle->value() );

    receipt->setSharpen( ui->horizontalSliderSharpen->value() );
    receipt->setShMasking( ui->horizontalSliderShMasking->value() );
    receipt->setChromaBlur( ui->horizontalSliderChromaBlur->value() );
    receipt->setHighlightReconstruction( ui->checkBoxHighLightReconstruction->isChecked() );
    receipt->setCamMatrixUsed( ui->comboBoxUseCameraMatrix->currentIndex() );
    receipt->setChromaSeparation( ui->checkBoxChromaSeparation->isChecked() );
    receipt->setProfile( ui->comboBoxProfile->currentIndex() );
    receipt->setTonemap( ui->comboBoxTonemapFct->currentIndex() );
    receipt->setTransferFunction( ui->lineEditTransferFunction->text() );
    receipt->setGamut( ui->comboBoxProcessingGamut->currentIndex() );
    receipt->setGamma( ui->horizontalSliderGamma->value() );
    receipt->setAllowCreativeAdjustments( ui->checkBoxCreativeAdjustments->isChecked() );
    receipt->setExrMode( ui->checkBoxExrMode->isChecked() );
    receipt->setAgx( ui->checkBoxAgX->isChecked() );
    receipt->setDenoiserStrength( ui->horizontalSliderDenoiseStrength->value() );
    receipt->setDenoiserWindow( ui->comboBoxDenoiseWindow->currentIndex() + 2 );
    receipt->setRbfDenoiserLuma( ui->horizontalSliderRbfDenoiseLuma->value() );
    receipt->setRbfDenoiserChroma( ui->horizontalSliderRbfDenoiseChroma->value() );
    receipt->setRbfDenoiserRange( ui->horizontalSliderRbfDenoiseRange->value() );
    receipt->setGrainStrength( ui->horizontalSliderGrainStrength->value() );
    receipt->setGrainLumaWeight( ui->horizontalSliderGrainLumaWeight->value() );

    receipt->setRawFixesEnabled( ui->checkBoxRawFixEnable->isChecked() );
    receipt->setVerticalStripes( toolButtonVerticalStripesCurrentIndex() );
    receipt->setFocusPixels( toolButtonFocusPixelsCurrentIndex() );
    receipt->setFpiMethod( toolButtonFocusPixelsIntMethodCurrentIndex() );
    receipt->setBadPixels( toolButtonBadPixelsCurrentIndex() );
    receipt->setBpsMethod( toolButtonBadPixelsSearchMethodCurrentIndex() );
    receipt->setBpiMethod( toolButtonBadPixelsIntMethodCurrentIndex() );
    receipt->setChromaSmooth( toolButtonChromaSmoothCurrentIndex() );
    receipt->setPatternNoise( toolButtonPatternNoiseCurrentIndex() );
    receipt->setUpsideDown( toolButtonUpsideDownCurrentIndex() );
    receipt->setDeflickerTarget( ui->spinBoxDeflickerTarget->value() );
    receipt->setDualIsoForced( llrpGetDualIsoValidity( m_pMlvObject ) );
    receipt->setDualIso( toolButtonDualIsoCurrentIndex() );
    receipt->setDualIsoPattern( ui->DualIsoPatternComboBox->currentIndex() );
    receipt->setDualIsoEvCorrection( ui->horizontalSliderDualIsoEvCorrection->value() );
    receipt->setDualIsoBlackDelta( ui->horizontalSliderDualIsoBlackDelta->value() );
    receipt->setDualIsoInterpolation( toolButtonDualIsoInterpolationCurrentIndex() );
    receipt->setDualIsoAliasMap( toolButtonDualIsoAliasMapCurrentIndex() );
    receipt->setDualIsoFrBlending( toolButtonDualIsoFullresBlendingCurrentIndex() );
    receipt->setDualIsoWhite( processingGetWhiteLevel( m_pMlvObject->processing ) );
    receipt->setDualIsoBlack( processingGetBlackLevel( m_pMlvObject->processing ) );
    receipt->setDarkFrameFileName( ui->lineEditDarkFrameFile->text() );
    receipt->setDarkFrameEnabled( toolButtonDarkFrameSubtractionCurrentIndex() );
    receipt->setRawBlack( ui->horizontalSliderRawBlack->value() );
    receipt->setRawWhite( ui->horizontalSliderRawWhite->value() );

    receipt->setTone( ui->horizontalSliderTone->value() );
    receipt->setToningStrength( ui->horizontalSliderToningStrength->value() );

    receipt->setLutEnabled( ui->checkBoxLutEnable->isChecked() );
    receipt->setLutName( ui->lineEditLutName->text() );
    receipt->setLutStrength( ui->horizontalSliderLutStrength->value() );

    receipt->setFilterEnabled( ui->checkBoxFilterEnable->isChecked() );
    receipt->setFilterIndex( ui->comboBoxFilterName->currentIndex() );
    receipt->setFilterStrength( ui->horizontalSliderFilterStrength->value() );

    receipt->setVignetteStrength( ui->horizontalSliderVignetteStrength->value() );
    receipt->setVignetteRadius( ui->horizontalSliderVignetteRadius->value() );
    receipt->setVignetteShape( ui->horizontalSliderVignetteShape->value() );
    receipt->setCaRed( ui->horizontalSliderCaRed->value() );
    receipt->setCaBlue( ui->horizontalSliderCaBlue->value() );
    receipt->setCaDesaturate( ui->horizontalSliderCaDesaturate->value() );
    receipt->setCaRadius( ui->horizontalSliderCaRadius->value() );

    receipt->setStretchFactorX( getHorizontalStretchFactor(true) );
    receipt->setStretchFactorY( getVerticalStretchFactor(true) );

    receipt->setCutIn( ui->spinBoxCutIn->value() );
    receipt->setCutOut( ui->spinBoxCutOut->value() );

    if( ui->actionPlaybackPosition->isChecked() ) receipt->setLastPlaybackPosition( ui->horizontalSliderPosition->value() );
    else receipt->setLastPlaybackPosition( 0 );

    receipt->setDebayer( ui->comboBoxDebayer->currentIndex() );

    receipt->setVidstabEnabled( ui->checkBoxVidstabEnable->isChecked() );
    receipt->setVidstabStepsize( ui->horizontalSliderVidstabStepsize->value() );
    receipt->setVidstabShakiness( ui->horizontalSliderVidstabShakiness->value() );
    receipt->setVidstabAccuracy( ui->horizontalSliderVidstabAccuracy->value() );
    receipt->setVidstabZoom( ui->horizontalSliderVidstabZoom->value() );
    receipt->setVidstabSmoothing( ui->horizontalSliderVidstabSmoothing->value() );
    receipt->setVidstabTripod( ui->checkBoxVidstabTripod->isChecked() );
}

//Replace receipt settings
void MainWindow::replaceReceipt(ReceiptSettings *receiptTarget, ReceiptSettings *receiptSource, bool paste)
{
    Ui::ReceiptCopyMaskDialog *cdui = m_pCopyMask->ui;

    if( paste && cdui->checkBoxExposure->isChecked() )   receiptTarget->setExposure( receiptSource->exposure() );
    if( paste && cdui->checkBoxContrast->isChecked() )   receiptTarget->setContrast( receiptSource->contrast() );
    if( paste && cdui->checkBoxPivot->isChecked() )      receiptTarget->setPivot( receiptSource->pivot() );
    if( paste && cdui->checkBoxWb->isChecked() )         receiptTarget->setTemperature( receiptSource->temperature() );
    if( paste && cdui->checkBoxWb->isChecked() )         receiptTarget->setTint( receiptSource->tint() );
    if( paste && cdui->checkBoxClarity->isChecked() )    receiptTarget->setClarity( receiptSource->clarity() );
    if( paste && cdui->checkBoxVibrance->isChecked() )   receiptTarget->setVibrance( receiptSource->vibrance() );
    if( paste && cdui->checkBoxSaturation->isChecked() ) receiptTarget->setSaturation( receiptSource->saturation() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setDs( receiptSource->ds() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setDr( receiptSource->dr() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setLs( receiptSource->ls() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setLr( receiptSource->lr() );
    if( paste && cdui->checkBoxCurve->isChecked() )      receiptTarget->setLightening( receiptSource->lightening() );
    if( paste && cdui->checkBoxGradationCurve->isChecked() ) receiptTarget->setGradationCurve( receiptSource->gradationCurve() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setHueVsHue( receiptSource->hueVsHue() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setHueVsSaturation( receiptSource->hueVsSaturation() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setHueVsLuminance( receiptSource->hueVsLuminance() );
    if( paste && cdui->checkBoxHslCurves->isChecked() )  receiptTarget->setLumaVsSaturation( receiptSource->lumaVsSaturation() );
    if( paste && cdui->checkBoxShadows->isChecked() )    receiptTarget->setShadows( receiptSource->shadows() );
    if( paste && cdui->checkBoxHighlights->isChecked() ) receiptTarget->setHighlights( receiptSource->highlights() );

    if( paste && cdui->checkBoxGradient->isChecked() )
    {
        receiptTarget->setGradientEnabled( receiptSource->isGradientEnabled() );
        receiptTarget->setGradientExposure( receiptSource->gradientExposure() );
        receiptTarget->setGradientContrast( receiptSource->gradientContrast() );
        receiptTarget->setGradientStartX( receiptSource->gradientStartX() );
        receiptTarget->setGradientStartY( receiptSource->gradientStartY() );
        receiptTarget->setGradientLength( receiptSource->gradientLength() );
        receiptTarget->setGradientAngle( receiptSource->gradientAngle() );
    }

    if( paste && cdui->checkBoxSharpen->isChecked() )
    {
        receiptTarget->setSharpen( receiptSource->sharpen() );
        receiptTarget->setShMasking( receiptSource->shMasking() );
    }
    if( paste && cdui->checkBoxChromaBlur->isChecked() ) receiptTarget->setChromaBlur( receiptSource->chromaBlur() );
    if( paste && cdui->checkBoxHighlightReconstruction->isChecked() ) receiptTarget->setHighlightReconstruction( receiptSource->isHighlightReconstruction() );
    if( paste && cdui->checkBoxCameraMatrix->isChecked() ) receiptTarget->setCamMatrixUsed( receiptSource->camMatrixUsed() );
    if( paste && cdui->checkBoxChromaBlur->isChecked() ) receiptTarget->setChromaSeparation( receiptSource->isChromaSeparation() );
    if( paste && cdui->checkBoxProfile->isChecked() )
    {
        receiptTarget->setProfile( receiptSource->profile() );
        receiptTarget->setAllowCreativeAdjustments( receiptSource->allowCreativeAdjustments() );
        receiptTarget->setExrMode( receiptSource->exrMode() );
        receiptTarget->setAgx( receiptSource->agx() );
        receiptTarget->setTonemap( receiptSource->tonemap() );
        receiptTarget->setTransferFunction( receiptSource->transferFunction() );
        receiptTarget->setGamut( receiptSource->gamut() );
        receiptTarget->setGamma( receiptSource->gamma() );
    }
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setDenoiserStrength( receiptSource->denoiserStrength() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setDenoiserWindow( receiptSource->denoiserWindow() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setRbfDenoiserLuma( receiptSource->rbfDenoiserLuma() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setRbfDenoiserChroma( receiptSource->rbfDenoiserChroma() );
    if( paste && cdui->checkBoxDenoise->isChecked() )    receiptTarget->setRbfDenoiserRange( receiptSource->rbfDenoiserRange() );
    if( paste && cdui->checkBoxGrain->isChecked() )      receiptTarget->setGrainStrength( receiptSource->grainStrength() );
    if( paste && cdui->checkBoxGrain->isChecked() )      receiptTarget->setGrainLumaWeight( receiptSource->grainLumaWeight() );

    if( paste && cdui->checkBoxRawCorrectEnable->isChecked() ) receiptTarget->setRawFixesEnabled( receiptSource->rawFixesEnabled() );
    if( paste && cdui->checkBoxDarkFrameSubtraction->isChecked() ) receiptTarget->setDarkFrameFileName( receiptSource->darkFrameFileName() );
    if( paste && cdui->checkBoxDarkFrameSubtraction->isChecked() ) receiptTarget->setDarkFrameEnabled( receiptSource->darkFrameEnabled() );
    if( paste && cdui->checkBoxVerticalStripes->isChecked() )  receiptTarget->setVerticalStripes( receiptSource->verticalStripes() );
    if( paste && cdui->checkBoxFoxusDots->isChecked() )        receiptTarget->setFocusPixels( receiptSource->focusPixels() );
    if( paste && cdui->checkBoxFoxusDots->isChecked() )        receiptTarget->setFpiMethod( receiptSource->fpiMethod() );
    if( paste && cdui->checkBoxBadPixels->isChecked() )        receiptTarget->setBadPixels( receiptSource->badPixels() );
    if( paste && cdui->checkBoxBadPixels->isChecked() )        receiptTarget->setBpsMethod( receiptSource->bpsMethod() );
    if( paste && cdui->checkBoxBadPixels->isChecked() )        receiptTarget->setBpiMethod( receiptSource->bpiMethod() );
    if( paste && cdui->checkBoxChromaSmooth->isChecked() )     receiptTarget->setChromaSmooth( receiptSource->chromaSmooth() );
    if( paste && cdui->checkBoxPatternNoise->isChecked() )     receiptTarget->setPatternNoise( receiptSource->patternNoise() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoForced( receiptSource->dualIsoForced() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIso( receiptSource->dualIso() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoAutoCorrected( receiptSource->dualIsoAutoCorrected() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoPattern( receiptSource->dualIsoPattern() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoEvCorrection( receiptSource->dualIsoEvCorrection() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoBlackDelta( receiptSource->dualIsoBlackDelta() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoInterpolation( receiptSource->dualIsoInterpolation() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoAliasMap( receiptSource->dualIsoAliasMap() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoFrBlending( receiptSource->dualIsoFrBlending() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoWhite( receiptSource->dualIsoWhite() );
    if( paste && cdui->checkBoxDualIso->isChecked() )          receiptTarget->setDualIsoBlack( receiptSource->dualIsoBlack() );
    if( paste && cdui->checkBoxRawBlackLevel->isChecked() )    receiptTarget->setRawBlack( receiptSource->rawBlack() );
    if( paste && cdui->checkBoxRawWhiteLevel->isChecked() )    receiptTarget->setRawWhite( receiptSource->rawWhite() );

    receiptTarget->setDeflickerTarget( receiptSource->deflickerTarget() );

    if( paste && cdui->checkBoxDebayer->isChecked() )          receiptTarget->setDebayer( receiptSource->debayer() );

    if( paste && cdui->checkBoxToning->isChecked() )
    {
        receiptTarget->setTone( receiptSource->tone() );
        receiptTarget->setToningStrength( receiptSource->toningStrength() );
    }

    if( paste && cdui->checkBoxLut->isChecked() )
    {
        receiptTarget->setLutEnabled( receiptSource->lutEnabled() );
        receiptTarget->setLutName( receiptSource->lutName() );
        receiptTarget->setLutStrength( receiptSource->lutStrength() );
    }

    if( paste && cdui->checkBoxFilter->isChecked() )
    {
        receiptTarget->setFilterEnabled( receiptSource->filterEnabled() );
        receiptTarget->setFilterIndex( receiptSource->filterIndex() );
        receiptTarget->setFilterStrength( receiptSource->filterStrength() );
    }
    if( paste && cdui->checkBoxVignette->isChecked() )
    {
        receiptTarget->setVignetteStrength( receiptSource->vignetteStrength() );
        receiptTarget->setVignetteRadius( receiptSource->vignetteRadius() );
        receiptTarget->setVignetteShape( receiptSource->vignetteShape() );
        receiptTarget->setCaRed( receiptSource->caRed() );
        receiptTarget->setCaBlue( receiptSource->caBlue() );
        receiptTarget->setCaDesaturate( receiptSource->caDesaturate() );
        receiptTarget->setCaRadius( receiptSource->caRadius() );
    }

    if( paste && cdui->checkBoxTransformation->isChecked() )
    {
        receiptTarget->setStretchFactorX( receiptSource->stretchFactorX() );
        receiptTarget->setStretchFactorY( receiptSource->stretchFactorY() );
        receiptTarget->setUpsideDown( receiptSource->upsideDown() );
        receiptTarget->setVidstabEnabled( receiptSource->vidStabEnabled() );
        receiptTarget->setVidstabStepsize( receiptSource->vidStabStepsize() );
        receiptTarget->setVidstabShakiness( receiptSource->vidStabShakiness() );
        receiptTarget->setVidstabAccuracy( receiptSource->vidStabAccuracy() );
        receiptTarget->setVidstabZoom( receiptSource->vidStabZoom() );
        receiptTarget->setVidstabSmoothing( receiptSource->vidStabSmoothing() );
        receiptTarget->setVidstabTripod( receiptSource->vidStabTripod() );
    }

    if( !paste )
    {
        receiptTarget->setCutIn( receiptSource->cutIn() );
        receiptTarget->setCutOut( receiptSource->cutOut() );
    }
}

//Reset the edit sliders to default
void MainWindow::on_actionResetReceipt_triggered()
{
    ReceiptSettings *receipt = new ReceiptSettings(); //default
    if( ui->actionUseDefaultReceipt->isChecked() ) resetReceiptWithDefault( receipt );
    receipt->setRawWhite( getMlvOriginalWhiteLevel( m_pMlvObject ) );
    receipt->setRawBlack( getMlvOriginalBlackLevel( m_pMlvObject ) * 10 );
    receipt->setDualIsoAutoCorrected( 0 );
    ACTIVE_RECEIPT->setDualIsoAutoCorrected( 0 );
    setSliders( receipt, false );
    delete receipt;
}

//Copy receipt to clipboard
void MainWindow::on_actionCopyRecept_triggered()
{
    if( SESSION_CLIP_COUNT <= 0 ) return;
    QModelIndexList list = selectedClipsList();
    if( list.size() > 1 )
    {
        QMessageBox::warning( this, APPNAME, tr( "Please select just one clip to copy a receipt!" ) );
        return;
    }

    int clipToCopy;
    if( list.size() == 0 ) clipToCopy = SESSION_ACTIVE_CLIP_ROW;
    else clipToCopy = m_pProxyModel->mapToSource( list.first() ).row();

    //Save slider receipt
    setReceipt( ACTIVE_RECEIPT );
    //Copy mask
    m_pCopyMask->exec();
    //Save selected to clipboard
    replaceReceipt( m_pReceiptClipboard, GET_RECEIPT( clipToCopy ), true );
    ui->actionPasteReceipt->setEnabled( true );
}

//Paste receipt from clipboard
void MainWindow::on_actionPasteReceipt_triggered()
{
    QModelIndexList list = selectedClipsList();
    if( list.size() )
    {
        for( int i = 0; i < list.size(); i++ )
        {
            //Do nothing for hidden clips
            if( ui->tableViewSession->isRowHidden( list.at( i ).row() ) ) continue;

            int row = m_pProxyModel->mapToSource( list.at( i ) ).row();
            pasteReceiptFromClipboardTo( row );
        }
    }
    else
    {
        pasteReceiptFromClipboardTo( SESSION_ACTIVE_CLIP_ROW );
    }
}

//Set/Reset default receipt
void MainWindow::on_actionUseDefaultReceipt_triggered(bool checked)
{
    if( !checked ) return;

    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open MLV App Receipt Xml"), m_defaultReceiptFileName,
                                           tr("MLV App Receipt Xml files (*.marxml)"));

    //Abort selected
    if( fileName.size() == 0 )
    {
        ui->actionUseDefaultReceipt->setChecked( false );
        return;
    }
    m_defaultReceiptFileName = fileName;
}
