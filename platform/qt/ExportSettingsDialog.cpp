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
ExportSettingsDialog::ExportSettingsDialog(QWidget *parent, Scripting *scripting, uint8_t currentCodecProfile, uint8_t currentCodecOption, uint8_t debayerMode, bool resize, uint16_t resizeWidth, uint16_t resizeHeight, bool fpsOverride, double fps, bool exportAudio, bool heightLocked) :
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
    on_checkBoxFpsOverride_toggled( fpsOverride );
    ui->doubleSpinBoxFps->setValue( fps );
    ui->checkBoxExportAudio->setChecked( exportAudio );
    ui->toolButtonLockHeight->setChecked( heightLocked );

    //Disable resize for AVFoundation
    if( ui->comboBoxOption->currentText() == QString( "Apple AVFoundation" ) )
    {
        on_comboBoxOption_currentIndexChanged( QString( "Apple AVFoundation" ) );
    }

#ifndef Q_OS_OSX
    //No scriptsupport for Windows and Linux
    ui->groupBoxScripting->setVisible( false );
#else
    m_pScripting = scripting;
    ui->comboBoxPostExportScript->blockSignals( true );
    ui->comboBoxPostExportScript->clear();
    ui->comboBoxPostExportScript->addItems( m_pScripting->getScriptNames() );
    ui->comboBoxPostExportScript->setCurrentText( m_pScripting->postExportScriptName() );
    ui->comboBoxPostExportScript->blockSignals( false );
#endif

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

//Get if height locked
bool ExportSettingsDialog::isHeightLocked()
{
    return ui->toolButtonLockHeight->isChecked();
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
    if( !ui->checkBoxFpsOverride->isChecked() ) ui->checkBoxExportAudio->setEnabled( true );
    ui->label_Info->clear();
    ui->label_Info->setToolTip( "" );

    if( index <= CODEC_PRORES4444 )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
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
        ui->labelDebayer->setEnabled( false );
        ui->comboBoxDebayer->setEnabled( false );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Default Naming Scheme" ) );
        ui->comboBoxOption->addItem( QString( "DaVinci Resolve Naming Scheme" ) );
        enableResize = false;
    }
    else if( index == CODEC_H264
          || index == CODEC_H265 )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
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
    else if( index == CODEC_MLV )
    {
        ui->labelDebayer->setEnabled( false );
        ui->comboBoxDebayer->setEnabled( false );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Fast Pass" ) );
        ui->comboBoxOption->addItem( QString( "Compressed" ) );
        ui->comboBoxOption->addItem( QString( "Averaged Frame" ) );
        ui->comboBoxOption->addItem( QString( "Extract Internal Darkframe" ) );
        enableResize = false;
    }
    else if( index == CODEC_DNXHD )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "1080p 10bit" ) );
        ui->comboBoxOption->addItem( QString( "1080p 8bit" ) );
        ui->comboBoxOption->addItem( QString( "720p 10bit" ) );
        ui->comboBoxOption->addItem( QString( "720p 8bit" ) );
        enableResize = false;
        QPixmap pic = QPixmap( ":/RetinaIMG/RetinaIMG/Status-dialog-warning-icon.png" ).scaled( 24 * devicePixelRatio(), 24 * devicePixelRatio() );
        pic.setDevicePixelRatio( devicePixelRatio() );
        ui->label_Info->setPixmap( pic );
        ui->label_Info->setToolTip( tr( "Note: DNxHD can only be exported @ 23.976, 25, 29.97, 50, 59.94 fps.\r\nPlease manually force to one of these, if your clips have diffent framerates!" ) );
    }
    else if( index == CODEC_DNXHR )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "444 1080p 10bit" ) );
        ui->comboBoxOption->addItem( QString( "HQX 1080p 10bit" ) );
        ui->comboBoxOption->addItem( QString( "HQ 1080p 8bit" ) );
        ui->comboBoxOption->addItem( QString( "SQ 1080p 8bit" ) );
        ui->comboBoxOption->addItem( QString( "LB 1080p 8bit" ) );
        enableResize = false;
        QPixmap pic = QPixmap( ":/RetinaIMG/RetinaIMG/Status-dialog-warning-icon.png" ).scaled( 24 * devicePixelRatio(), 24 * devicePixelRatio() );
        pic.setDevicePixelRatio( devicePixelRatio() );
        ui->label_Info->setPixmap( pic );
        ui->label_Info->setToolTip( tr( "Note: DNxHR can only be exported @ 23.976, 25, 29.97, 50, 59.94 fps.\r\nPlease manually force to one of these, if your clips have diffent framerates!" ) );
    }
    else if( index == CODEC_AUDIO_ONLY )
    {
        ui->labelDebayer->setEnabled( false );
        ui->comboBoxDebayer->setEnabled( false );
        ui->comboBoxOption->setEnabled( false );
        ui->checkBoxExportAudio->setEnabled( false );
        ui->checkBoxExportAudio->setChecked( true );
        enableResize = false;
    }
    else
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( false );
    }

    //If CDNG / MLV, disable resize feature
    if( !enableResize )
    {
        ui->checkBoxResize->setChecked( false );
    }
    ui->checkBoxResize->setEnabled( enableResize );

    //En-/disable fps override
    if( ( index == CODEC_MLV ) || ( index == CODEC_TIFF ) || ( index == CODEC_AUDIO_ONLY ) )
    {
        ui->checkBoxFpsOverride->setEnabled( false );
        ui->checkBoxFpsOverride->setChecked( false );
    }
    else
    {
        ui->checkBoxFpsOverride->setEnabled( true );
    }
}

//Change settings if FPS Override is clicked
void ExportSettingsDialog::on_checkBoxFpsOverride_toggled(bool checked)
{
    //Do nothing for WAV export
    if( ui->comboBoxCodec->currentIndex() == CODEC_AUDIO_ONLY ) return;

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
    ui->toolButtonLockHeight->setEnabled( checked );
    ui->spinBoxWidth->setEnabled( checked );

    if( checked && !ui->toolButtonLockHeight->isChecked() ) ui->spinBoxHeight->setEnabled( true );
    else ui->spinBoxHeight->setEnabled( false );
}

//Disable audio & resize for AVFoundation
void ExportSettingsDialog::on_comboBoxOption_currentIndexChanged(const QString &arg1)
{
    if( arg1 == QString( "Apple AVFoundation" ) )
    {
        ui->checkBoxResize->setChecked( false );
        ui->checkBoxResize->setEnabled( false );
    }
    else
    {
        if( ( ui->comboBoxCodec->currentIndex() != CODEC_MLV )
         && ( ui->comboBoxCodec->currentIndex() != CODEC_CDNG )
         && ( ui->comboBoxCodec->currentIndex() != CODEC_CDNG_LOSSLESS )
         && ( ui->comboBoxCodec->currentIndex() != CODEC_CDNG_FAST )
         && ( ui->comboBoxCodec->currentIndex() != CODEC_DNXHD )
         && ( ui->comboBoxCodec->currentIndex() != CODEC_DNXHR ) )
        {
            ui->checkBoxExportAudio->setEnabled( true );
            ui->checkBoxResize->setEnabled( true );
        }
    }
}

//Toggle height lock
void ExportSettingsDialog::on_toolButtonLockHeight_toggled(bool checked)
{
    if( checked ) ui->toolButtonLockHeight->setIcon( QIcon( ":/RetinaIMG/RetinaIMG/Actions-document-encrypt-icon.png" ) );
    else ui->toolButtonLockHeight->setIcon( QIcon( ":/RetinaIMG/RetinaIMG/Actions-document-decrypt-icon.png" ) );

    if( !checked && ui->checkBoxResize->isChecked() ) ui->spinBoxHeight->setEnabled( true );
    else ui->spinBoxHeight->setEnabled( false );

}

//Post export script chosen
void ExportSettingsDialog::on_comboBoxPostExportScript_currentIndexChanged(const QString &arg1)
{
    m_pScripting->setPostExportScript( arg1 );
}
