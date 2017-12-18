/*!
 * \file ExportSettingsDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Select codec
 */

#include "ExportSettingsDialog.h"
#include "ui_ExportSettingsDialog.h"
#include <QMessageBox>
#include <QStandardItemModel>
#include <QStandardItem>

//Constructor
ExportSettingsDialog::ExportSettingsDialog(QWidget *parent, uint8_t currentCodecProfile, uint8_t currentCodecOption, uint8_t debayerMode, bool resize, uint16_t resizeWidth, uint16_t resizeHeight, bool fpsOverride, double fps, bool exportAudio) :
    QDialog(parent),
    ui(new Ui::ExportSettingsDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    ui->comboBoxCodec->setCurrentIndex( currentCodecProfile );
    on_comboBoxCodec_currentIndexChanged( currentCodecProfile );
    ui->comboBoxOption->setCurrentIndex( currentCodecOption );
    ui->comboBoxDebayer->setCurrentIndex( debayerMode );
    ui->checkBoxResize->setChecked( resize );
    on_checkBoxResize_toggled( resize );
    ui->spinBoxWidth->setValue( resizeWidth );
    ui->spinBoxHeight->setValue( resizeHeight );
    ui->checkBoxFpsOverride->setChecked( fpsOverride );
    on_checkBoxFpsOverride_clicked( fpsOverride );
    ui->doubleSpinBoxFps->setValue( fps );
    ui->checkBoxExportAudio->setChecked( exportAudio );

    //Disable audio & resize for AVFoundation
    if( ui->comboBoxOption->currentText() == QString( "Apple AVFoundation" ) )
    {
        on_comboBoxOption_currentIndexChanged( QString( "Apple AVFoundation" ) );
    }

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

//Get if resize is enabled
bool ExportSettingsDialog::isResizeEnabled()
{
    return ui->checkBoxResize->isChecked();
}

//Get resize width
uint16_t ExportSettingsDialog::resizeWidth()
{
    return ui->spinBoxWidth->value();
}

//Get resize height
uint16_t ExportSettingsDialog::resizeHeight()
{
    return ui->spinBoxHeight->value();
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
    bool enableResize = true;

    ui->comboBoxOption->clear();

    if( index <= CODEC_PRORES4444 )
    {
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "ffmpeg Kostya" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg Anatolyi" ) );
#ifdef Q_OS_MACX
        if( index == CODEC_PRORES422ST || index == CODEC_PRORES4444 )
        {
            ui->comboBoxOption->addItem( QString( "Apple AVFoundation" ) );
        }
#endif
        if( index == CODEC_PRORES4444 )
        {
            QStandardItemModel* model = qobject_cast<QStandardItemModel*>(ui->comboBoxOption->model());
            QStandardItem* item = model->item(1);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
    }
    else if( index == CODEC_CDNG
          || index == CODEC_CDNG_LOSSLESS
          || index == CODEC_CDNG_FAST )
    {
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Default Naming Scheme" ) );
        ui->comboBoxOption->addItem( QString( "DaVinci Resolve Naming Scheme" ) );
        enableResize = false;
    }
    else if( index == CODEC_H264
          || index == CODEC_H265 )
    {
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "ffmpeg Movie (*.mov)" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg MPEG-4 (*.mp4)" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg Matroska (*.mkv)" ) );
#ifdef Q_OS_MACX
        if( index == CODEC_H264 )
        {
            ui->comboBoxOption->addItem( QString( "Apple AVFoundation" ) );
        }
#endif
    }
    else
    {
        ui->comboBoxOption->setEnabled( false );
    }

    //If CDNG, disable resize feature
    if( !enableResize )
    {
        ui->checkBoxResize->setChecked( false );
    }
    ui->checkBoxResize->setEnabled( enableResize );
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

//Enable / Disable elements when resize is checked
void ExportSettingsDialog::on_checkBoxResize_toggled(bool checked)
{
    ui->spinBoxWidth->setEnabled( checked );
    ui->spinBoxHeight->setEnabled( checked );
}

//Disable audio & resize for AVFoundation
void ExportSettingsDialog::on_comboBoxOption_currentIndexChanged(const QString &arg1)
{
    if( arg1 == QString( "Apple AVFoundation" ) )
    {
        ui->checkBoxExportAudio->setChecked( false );
        ui->checkBoxExportAudio->setEnabled( false );

        ui->checkBoxResize->setChecked( false );
        ui->checkBoxResize->setEnabled( false );
    }
    else
    {
        ui->checkBoxExportAudio->setEnabled( true );
        ui->checkBoxResize->setEnabled( true );
    }
}
