/*!
 * \file ExportSettingsDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Select codec
 */

#include "ExportSettingsDialog.h"
#include "ui_ExportSettingsDialog.h"
#include <QMessageBox>

//Constructor
ExportSettingsDialog::ExportSettingsDialog(QWidget *parent, uint8_t currentCodecProfile, uint8_t currentCodecOption, uint8_t debayerMode, bool fpsOverride, double fps, bool exportAudio) :
    QDialog(parent),
    ui(new Ui::ExportSettingsDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    ui->comboBoxCodec->setCurrentIndex( currentCodecProfile );
    on_comboBoxCodec_currentIndexChanged( currentCodecProfile );
    ui->comboBoxOption->setCurrentIndex( currentCodecOption );
    ui->comboBoxDebayer->setCurrentIndex( debayerMode );
    ui->checkBoxFpsOverride->setChecked( fpsOverride );
    on_checkBoxFpsOverride_clicked( fpsOverride );
    ui->doubleSpinBoxFps->setValue( fps );
    ui->checkBoxExportAudio->setChecked( exportAudio );

    adjustSize();
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

//Get Codec Option
uint8_t ExportSettingsDialog::encoderOption()
{
    return ui->comboBoxOption->currentIndex();
}

//Get Debayer Mode
uint8_t ExportSettingsDialog::debayerMode()
{
    return ui->comboBoxDebayer->currentIndex();
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

//Close clicked
void ExportSettingsDialog::on_pushButtonClose_clicked()
{
    close();
}

//Change option when codec changed
void ExportSettingsDialog::on_comboBoxCodec_currentIndexChanged(int index)
{
    ui->comboBoxOption->clear();

    if( index <= CODEC_PRORES422HQ )
    {
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Kostya" ) );
        ui->comboBoxOption->addItem( QString( "Anatolyi (faster)" ) );
    }
    else if( index == CODEC_CDNG
          || index == CODEC_CDNG_LOSSLESS
          || index == CODEC_CDNG_FAST )
    {
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Default Naming Scheme" ) );
        ui->comboBoxOption->addItem( QString( "DaVinci Resolve Naming Scheme" ) );
    }
    else if( index == CODEC_H264
          || index == CODEC_H265 )
    {
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Movie (*.mov)" ) );
        ui->comboBoxOption->addItem( QString( "MPEG-4 (*.mp4)" ) );
        ui->comboBoxOption->addItem( QString( "Matroska (*.mkv)" ) );
    }
    else
    {
        ui->comboBoxOption->setEnabled( false );
        if( index == CODEC_PRORES4444 ) ui->comboBoxOption->addItem( QString( "Kostya" ) );
    }
}

//Change settings if FPS Override is clicked
void ExportSettingsDialog::on_checkBoxFpsOverride_clicked(bool checked)
{
    //if override is checked, export audio is not possible, so disable and grey out
    if( checked )
    {
        ui->checkBoxExportAudio->setChecked( false );
    }
    ui->checkBoxExportAudio->setEnabled( !checked );
}
