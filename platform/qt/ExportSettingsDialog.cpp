/*!
 * \file ExportSettingsDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Select codec
 */

#include "ExportSettingsDialog.h"
#include "ui_ExportSettingsDialog.h"

//Constructor
ExportSettingsDialog::ExportSettingsDialog(QWidget *parent, uint8_t currentCodecProfile, uint8_t previewMode, bool fpsOverride, double fps, bool exportAudio) :
    QDialog(parent),
    ui(new Ui::ExportSettingsDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    ui->comboBoxCodec->setCurrentIndex( currentCodecProfile );
    if( previewMode == 1 ) ui->radioButtonPreviewList->setChecked( true );
    else if( previewMode == 2 ) ui->radioButtonPreviewIcon->setChecked( true );
    else ui->radioButtonPreviewDisabled->setChecked( true );
    ui->checkBoxFpsOverride->setChecked( fpsOverride );
    ui->doubleSpinBoxFps->setValue( fps );
    ui->checkBoxExportAudio->setChecked( exportAudio );
}

//Destructor
ExportSettingsDialog::~ExportSettingsDialog()
{
    delete ui;
}

//Get Codec Profile
uint8_t ExportSettingsDialog::encoderSetting(void)
{
    return ui->comboBoxCodec->currentIndex();
}

//Get Preview Mode
uint8_t ExportSettingsDialog::previewMode()
{
    if( ui->radioButtonPreviewList->isChecked() ) return 1;
    if( ui->radioButtonPreviewIcon->isChecked() ) return 2;
    else return 0;
}

//Get if fps override
bool ExportSettingsDialog::isFpsOverride()
{
    return ui->checkBoxFpsOverride->isChecked();
}

//Get fps
double ExportSettingsDialog::getFps()
{
    return ui->doubleSpinBoxFps->value();
}

//Get export audio checkbox checked
bool ExportSettingsDialog::isExportAudioEnabled()
{
    return ui->checkBoxExportAudio->isChecked();
}
