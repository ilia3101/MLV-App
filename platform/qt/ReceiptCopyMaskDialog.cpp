/*!
 * \file ReceiptCopyMaskDialog.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief A dialog to setup a receipt mask
 */

#include "ReceiptCopyMaskDialog.h"

ReceiptCopyMaskDialog::ReceiptCopyMaskDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReceiptCopyMaskDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    adjustSize();

    connect( ui->checkBoxRawCorrectEnable, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxDarkFrameSubtraction, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxFoxusDots, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxBadPixels, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxChromaSmooth, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxVerticalStripes, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxDualIso, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxPatternNoise, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxRawBlackLevel, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );
    connect( ui->checkBoxRawWhiteLevel, SIGNAL(clicked(bool)), this, SLOT(checkBoxRawCorrectionState()) );

    connect( ui->checkBoxExposure, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxContrast, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxPivot, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxWb, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxClarity, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxVibrance, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxSaturation, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxCurve, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxHighlights, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxShadows, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxGradationCurve, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxHighlightReconstruction, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxCameraMatrix, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );
    connect( ui->checkBoxProfile, SIGNAL(clicked(bool)), this, SLOT(checkBoxProcessingState()) );

    connect( ui->checkBoxSharpen, SIGNAL(clicked(bool)), this, SLOT(checkBoxDetailsState()) );
    connect( ui->checkBoxChromaBlur, SIGNAL(clicked(bool)), this, SLOT(checkBoxDetailsState()) );
    connect( ui->checkBoxDenoise, SIGNAL(clicked(bool)), this, SLOT(checkBoxDetailsState()) );
    connect( ui->checkBoxGrain, SIGNAL(clicked(bool)), this, SLOT(checkBoxDetailsState()) );

    on_pushButtonAll_clicked();
    ui->checkBoxRawBlackLevel->setChecked( false );
    ui->checkBoxRawWhiteLevel->setChecked( false );
    checkBoxRawCorrectionState();
}

ReceiptCopyMaskDialog::~ReceiptCopyMaskDialog()
{
    delete ui;
}

void ReceiptCopyMaskDialog::checkBoxRawCorrectionState()
{
    if( ui->checkBoxRawCorrectEnable->isChecked()
     && ui->checkBoxDarkFrameSubtraction->isChecked()
     && ui->checkBoxFoxusDots->isChecked()
     && ui->checkBoxBadPixels->isChecked()
     && ui->checkBoxChromaSmooth->isChecked()
     && ui->checkBoxVerticalStripes->isChecked()
     && ui->checkBoxDualIso->isChecked()
     && ui->checkBoxPatternNoise->isChecked()
     && ui->checkBoxRawBlackLevel->isChecked()
     && ui->checkBoxRawWhiteLevel->isChecked() )
    {
        ui->checkBoxRawCorrection->setTristate( false );
        ui->checkBoxRawCorrection->setCheckState( Qt::Checked );
    }
    else if( !ui->checkBoxRawCorrectEnable->isChecked()
          && !ui->checkBoxDarkFrameSubtraction->isChecked()
          && !ui->checkBoxFoxusDots->isChecked()
          && !ui->checkBoxBadPixels->isChecked()
          && !ui->checkBoxChromaSmooth->isChecked()
          && !ui->checkBoxVerticalStripes->isChecked()
          && !ui->checkBoxDualIso->isChecked()
          && !ui->checkBoxPatternNoise->isChecked()
          && !ui->checkBoxRawBlackLevel->isChecked()
          && !ui->checkBoxRawWhiteLevel->isChecked() )
    {
        ui->checkBoxRawCorrection->setTristate( false );
        ui->checkBoxRawCorrection->setCheckState( Qt::Unchecked );
    }
    else
    {
        ui->checkBoxRawCorrection->setTristate( true );
        ui->checkBoxRawCorrection->setCheckState( Qt::PartiallyChecked );
    }
}

void ReceiptCopyMaskDialog::checkBoxProcessingState()
{
    if( ui->checkBoxExposure->isChecked()
     && ui->checkBoxContrast->isChecked()
     && ui->checkBoxPivot->isChecked()
     && ui->checkBoxWb->isChecked()
     && ui->checkBoxClarity->isChecked()
     && ui->checkBoxVibrance->isChecked()
     && ui->checkBoxSaturation->isChecked()
     && ui->checkBoxCurve->isChecked()
     && ui->checkBoxHighlights->isChecked()
     && ui->checkBoxShadows->isChecked()
     && ui->checkBoxGradationCurve->isChecked()
     && ui->checkBoxHighlightReconstruction->isChecked()
     && ui->checkBoxCameraMatrix->isChecked()
     && ui->checkBoxProfile->isChecked() )
    {
        ui->checkBoxProcessing->setTristate( false );
        ui->checkBoxProcessing->setCheckState( Qt::Checked );
    }
    else if( !ui->checkBoxExposure->isChecked()
          && !ui->checkBoxContrast->isChecked()
          && !ui->checkBoxPivot->isChecked()
          && !ui->checkBoxWb->isChecked()
          && !ui->checkBoxClarity->isChecked()
          && !ui->checkBoxVibrance->isChecked()
          && !ui->checkBoxSaturation->isChecked()
          && !ui->checkBoxCurve->isChecked()
          && !ui->checkBoxHighlights->isChecked()
          && !ui->checkBoxShadows->isChecked()
          && !ui->checkBoxGradationCurve->isChecked()
          && !ui->checkBoxHighlightReconstruction->isChecked()
          && !ui->checkBoxCameraMatrix->isChecked()
          && !ui->checkBoxProfile->isChecked() )
    {
        ui->checkBoxProcessing->setTristate( false );
        ui->checkBoxProcessing->setCheckState( Qt::Unchecked );
    }
    else
    {
        ui->checkBoxProcessing->setTristate( true );
        ui->checkBoxProcessing->setCheckState( Qt::PartiallyChecked );
    }
}

void ReceiptCopyMaskDialog::checkBoxDetailsState()
{
    if( ui->checkBoxSharpen->isChecked()
     && ui->checkBoxChromaBlur->isChecked()
     && ui->checkBoxDenoise->isChecked()
     && ui->checkBoxGrain->isChecked() )
    {
        ui->checkBoxDetails->setTristate( false );
        ui->checkBoxDetails->setCheckState( Qt::Checked );
    }
    else if( !ui->checkBoxSharpen->isChecked()
          && !ui->checkBoxChromaBlur->isChecked()
          && !ui->checkBoxDenoise->isChecked()
          && !ui->checkBoxGrain->isChecked() )
    {
        ui->checkBoxDetails->setTristate( false );
        ui->checkBoxDetails->setCheckState( Qt::Unchecked );
    }
    else
    {
        ui->checkBoxDetails->setTristate( true );
        ui->checkBoxDetails->setCheckState( Qt::PartiallyChecked );
    }
}

void ReceiptCopyMaskDialog::on_checkBoxRawCorrection_clicked(bool checked)
{
    ui->checkBoxRawCorrection->setTristate( false );

    ui->checkBoxRawCorrectEnable->setChecked( checked );
    ui->checkBoxDarkFrameSubtraction->setChecked( checked );
    ui->checkBoxFoxusDots->setChecked( checked );
    ui->checkBoxBadPixels->setChecked( checked );
    ui->checkBoxChromaSmooth->setChecked( checked );
    ui->checkBoxVerticalStripes->setChecked( checked );
    ui->checkBoxDualIso->setChecked( checked );
    ui->checkBoxPatternNoise->setChecked( checked );
    ui->checkBoxRawBlackLevel->setChecked( checked );
    ui->checkBoxRawWhiteLevel->setChecked( checked );
}

void ReceiptCopyMaskDialog::on_checkBoxProcessing_clicked(bool checked)
{
    ui->checkBoxProcessing->setTristate( false );

    ui->checkBoxExposure->setChecked( checked );
    ui->checkBoxContrast->setChecked( checked );
    ui->checkBoxPivot->setChecked( checked );
    ui->checkBoxWb->setChecked( checked );
    ui->checkBoxClarity->setChecked( checked );
    ui->checkBoxVibrance->setChecked( checked );
    ui->checkBoxSaturation->setChecked( checked );
    ui->checkBoxCurve->setChecked( checked );
    ui->checkBoxHighlights->setChecked( checked );
    ui->checkBoxShadows->setChecked( checked );
    ui->checkBoxGradationCurve->setChecked( checked );
    ui->checkBoxHighlightReconstruction->setChecked( checked );
    ui->checkBoxCameraMatrix->setChecked( checked );
    ui->checkBoxProfile->setChecked( checked );
}

void ReceiptCopyMaskDialog::on_checkBoxDetails_clicked(bool checked)
{
    ui->checkBoxDetails->setTristate( false );

    ui->checkBoxSharpen->setChecked( checked );
    ui->checkBoxChromaBlur->setChecked( checked );
    ui->checkBoxDenoise->setChecked( checked );
    ui->checkBoxGrain->setChecked( checked );
}

void ReceiptCopyMaskDialog::on_pushButtonAll_clicked()
{
    ui->checkBoxRawCorrection->setTristate( false );
    ui->checkBoxRawCorrection->setCheckState( Qt::Checked );
    ui->checkBoxProcessing->setTristate( false );
    ui->checkBoxProcessing->setCheckState( Qt::Checked );
    ui->checkBoxDetails->setTristate( false );
    ui->checkBoxDetails->setCheckState( Qt::Checked );

    ui->checkBoxRawCorrection->setChecked( true );
    on_checkBoxRawCorrection_clicked( true );
    ui->checkBoxProcessing->setChecked( true );
    on_checkBoxProcessing_clicked( true );
    ui->checkBoxDebayer->setChecked( true );
    ui->checkBoxDetails->setChecked( true );
    on_checkBoxDetails_clicked( true );
    ui->checkBoxGradient->setChecked( true );
    ui->checkBoxHslCurves->setChecked( true );
    ui->checkBoxToning->setChecked( true );
    ui->checkBoxLut->setChecked( true );
    ui->checkBoxFilter->setChecked( true );
    ui->checkBoxVignette->setChecked( true );
    ui->checkBoxTransformation->setChecked( true );
}

void ReceiptCopyMaskDialog::on_pushButtonNone_clicked()
{
    ui->checkBoxRawCorrection->setTristate( false );
    ui->checkBoxRawCorrection->setCheckState( Qt::Unchecked );
    ui->checkBoxProcessing->setTristate( false );
    ui->checkBoxProcessing->setCheckState( Qt::Unchecked );
    ui->checkBoxDetails->setTristate( false );
    ui->checkBoxDetails->setCheckState( Qt::Unchecked );

    ui->checkBoxRawCorrection->setChecked( false );
    on_checkBoxRawCorrection_clicked( false );
    ui->checkBoxProcessing->setChecked( false );
    on_checkBoxProcessing_clicked( false );
    ui->checkBoxDebayer->setChecked( false );
    ui->checkBoxDetails->setChecked( false );
    on_checkBoxDetails_clicked( false );
    ui->checkBoxGradient->setChecked( false );
    ui->checkBoxHslCurves->setChecked( false );
    ui->checkBoxToning->setChecked( false );
    ui->checkBoxLut->setChecked( false );
    ui->checkBoxFilter->setChecked( false );
    ui->checkBoxVignette->setChecked( false );
    ui->checkBoxTransformation->setChecked( false );
}
