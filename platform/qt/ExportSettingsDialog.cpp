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
ExportSettingsDialog::ExportSettingsDialog(QWidget *parent, uint8_t currentCodecProfile, uint8_t currentCodecOption, uint8_t previewMode, bool fpsOverride, double fps, bool exportAudio, int style) :
    QDialog(parent),
    ui(new Ui::ExportSettingsDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    ui->comboBoxCodec->setCurrentIndex( currentCodecProfile );
    ui->comboBoxOption->setCurrentIndex( currentCodecOption );
    if( previewMode == 1 ) ui->radioButtonPreviewList->setChecked( true );
    else if( previewMode == 2 ) ui->radioButtonPreviewIcon->setChecked( true );
    else ui->radioButtonPreviewDisabled->setChecked( true );
    ui->checkBoxFpsOverride->setChecked( fpsOverride );
    ui->doubleSpinBoxFps->setValue( fps );
    ui->checkBoxExportAudio->setChecked( exportAudio );
    ui->comboBoxStyle->setCurrentIndex( style );
    m_styleAtStart = style;
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

//Get Style Selection
int ExportSettingsDialog::getStyleIndex()
{
    return ui->comboBoxStyle->currentIndex();
}

//Get export audio checkbox checked
bool ExportSettingsDialog::isExportAudioEnabled()
{
    return ui->checkBoxExportAudio->isChecked();
}

//Close clicked
void ExportSettingsDialog::on_pushButtonClose_clicked()
{
    if( m_styleAtStart != ui->comboBoxStyle->currentIndex() )
    {
        QMessageBox::information( this, tr( "Style change" ), tr( "Appearance will be changed on next application start." ) );
    }
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
    else
    {
        ui->comboBoxOption->setEnabled( false );
        if( index == CODEC_PRORES4444 ) ui->comboBoxOption->addItem( QString( "Kostya" ) );
        if( index == CODEC_CDNG ) QMessageBox::information( this, tr( "Export Setting Info" ), tr( "Note: CDNG export is not yet implemented. Use it for testing only..." ) );
    }
}
