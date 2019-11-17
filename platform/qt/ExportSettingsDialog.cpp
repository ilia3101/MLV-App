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
#include <QSettings>
#include <QList>
#include <QDataStream>
#include <QDebug>

//From here for Preset Saving
struct ExportPreset {
    QString name;
    uint8_t currentCodecProfile;
    uint8_t currentCodecOption;
    uint8_t debayerMode;
    bool resize;
    uint16_t resizeWidth;
    uint16_t resizeHeight;
    bool fpsOverride;
    double fps;
    bool exportAudio;
    bool heightLocked;
    uint8_t smooth;
    uint8_t scaleAlgo; //not used
    bool hdrBlending;
};

Q_DECLARE_METATYPE(ExportPreset);

QDataStream& operator<<(QDataStream& out, const ExportPreset& v) {
    out << v.name
        << v.currentCodecProfile
        << v.currentCodecOption
        << v.debayerMode
        << v.resize
        << v.resizeWidth
        << v.resizeHeight
        << v.fpsOverride
        << v.fps
        << v.exportAudio
        << v.heightLocked
        << v.smooth
        << v.scaleAlgo //not used
        << v.hdrBlending;
    return out;
}

QDataStream& operator>>(QDataStream& in, ExportPreset& v) {
    in >> v.name;
    in >> v.currentCodecProfile;
    in >> v.currentCodecOption;
    in >> v.debayerMode;
    in >> v.resize;
    in >> v.resizeWidth;
    in >> v.resizeHeight;
    in >> v.fpsOverride;
    in >> v.fps;
    in >> v.exportAudio;
    in >> v.heightLocked;
    in >> v.smooth;
    in >> v.scaleAlgo;  //not used
    in >> v.hdrBlending;
    return in;
}

// Compare two variants.
bool exportPresetLessThan(const ExportPreset &v1, const ExportPreset &v2)
{
    return v1.name < v2.name;
}
//Until here for Preset Saving

//Constructor
ExportSettingsDialog::ExportSettingsDialog(QWidget *parent, Scripting *scripting, uint8_t currentCodecProfile, uint8_t currentCodecOption, uint8_t debayerMode, bool resize, uint16_t resizeWidth, uint16_t resizeHeight, bool fpsOverride, double fps, bool exportAudio, bool heightLocked, uint8_t smooth, bool hdrBlending) :
    QDialog(parent),
    ui(new Ui::ExportSettingsDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    m_blockOnce = true;
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
    ui->comboBoxSmoothing->setCurrentIndex( smooth );
    ui->checkBoxHdrBlending->setChecked( hdrBlending );

    //Disable some options for AVFoundation
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

    //Preset list
    qRegisterMetaTypeStreamOperators<QList<ExportPreset> >("QList<ExportPreset>");
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    QList<ExportPreset> presetList = set.value( "ExportPresets" ).value<QList<ExportPreset> >();
    ui->listWidget->blockSignals( true );
    for( int i = 0; i < presetList.count(); i++ )
    {
        QListWidgetItem *item = new QListWidgetItem( presetList.at(i).name );
        item->setFlags( item->flags() | Qt::ItemIsEditable );
        ui->listWidget->addItem( item );
    }
    ui->listWidget->blockSignals( false );

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

//Get if smooth filter is enabled
uint8_t ExportSettingsDialog::smoothSetting()
{
    return ui->comboBoxSmoothing->currentIndex();
}

bool ExportSettingsDialog::hdrBlending()
{
    return ui->checkBoxHdrBlending->isChecked();
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
    else if( index == CODEC_AVI )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "YUV420 8bit" ) );
        ui->comboBoxOption->addItem( QString( "V210 10bit" ) );
        ui->comboBoxOption->addItem( QString( "BGR24 8bit" ) );
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
        ui->comboBoxOption->addItem( QString( "ffmpeg Movie (*.mov) High Quality" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg MPEG-4 (*.mp4) High Quality" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg Matroska (*.mkv) High Quality" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg Movie (*.mov) Medium Quality" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg MPEG-4 (*.mp4) Medium Quality" ) );
        ui->comboBoxOption->addItem( QString( "ffmpeg Matroska (*.mkv) Medium Quality" ) );
#ifdef Q_OS_MACX
        if( index == CODEC_H264 )
        {
            ui->comboBoxOption->addItem( QString( "Apple AVFoundation" ) );
        }
#endif
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
        if( index == CODEC_H265 )
        {
            ui->comboBoxOption->addItem( QString( "Apple AVFoundation" ) );
        }
#endif
    }
    else if( index == CODEC_TIFF )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Sequence" ) );
        ui->comboBoxOption->addItem( QString( "Averaged Frame (max. 128 frames)" ) );
    }
    else if( index == CODEC_PNG )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "16 bit" ) );
        ui->comboBoxOption->addItem( QString( "8 bit" ) );
    }
    else if( index == CODEC_JPG2K )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Sequence (*.jp2)" ) );
        ui->comboBoxOption->addItem( QString( "Movie (*.mov)" ) );
    }
    else if( index == CODEC_FFVHUFF )
    {
        ui->labelDebayer->setEnabled( true );
        ui->comboBoxDebayer->setEnabled( true );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "10bit YUV 4:4:4" ) );
        ui->comboBoxOption->addItem( QString( "12bit YUV 4:4:4" ) );
        ui->comboBoxOption->addItem( QString( "16bit YUV 4:4:4" ) );
        ui->comboBoxOption->setCurrentIndex( 2 );
    }
    else if( index == CODEC_MLV )
    {
        ui->labelDebayer->setEnabled( false );
        ui->comboBoxDebayer->setEnabled( false );
        ui->comboBoxOption->setEnabled( true );
        ui->comboBoxOption->addItem( QString( "Fast Pass" ) );
        ui->comboBoxOption->addItem( QString( "Compress" ) );
        ui->comboBoxOption->addItem( QString( "Decompress" ) );
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
        ui->comboBoxSmoothing->setCurrentIndex( 0 );
        on_comboBoxSmoothing_currentIndexChanged( 0 );
        ui->checkBoxHdrBlending->setChecked( false );
        on_checkBoxHdrBlending_toggled( false );
    }
    ui->checkBoxResize->setEnabled( enableResize );
    on_comboBoxSmoothing_currentIndexChanged( ui->comboBoxSmoothing->currentIndex() );
    on_checkBoxHdrBlending_toggled( ui->checkBoxHdrBlending->isChecked() );
    ui->comboBoxSmoothing->setEnabled( enableResize );
    ui->label_Smoothing->setEnabled( enableResize );
    ui->checkBoxHdrBlending->setEnabled( enableResize );

    //En-/disable fps override
    if( ( index == CODEC_MLV ) || ( index == CODEC_TIFF ) || ( index == CODEC_PNG ) || ( index == CODEC_JPG2K ) || ( index == CODEC_AUDIO_ONLY ) )
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

//Disable some options for AVFoundation
void ExportSettingsDialog::on_comboBoxOption_currentIndexChanged(const QString &arg1)
{
    if( arg1 == QString( "Apple AVFoundation" ) )
    {
        ui->comboBoxSmoothing->setCurrentIndex( 0 );
        ui->comboBoxSmoothing->setEnabled( false );
        ui->label_Smoothing->setEnabled( false );
        ui->checkBoxHdrBlending->setChecked( false );
        ui->checkBoxHdrBlending->setEnabled( false );
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
            ui->comboBoxSmoothing->setEnabled( true );
            ui->label_Smoothing->setEnabled( true );
            ui->checkBoxHdrBlending->setEnabled( true );
            on_comboBoxSmoothing_currentIndexChanged( ui->comboBoxSmoothing->currentIndex() );
            on_checkBoxHdrBlending_toggled( ui->checkBoxHdrBlending->isChecked() );
            on_checkBoxFpsOverride_toggled( ui->checkBoxFpsOverride->isChecked() );
        }

        //En-/disable fps override
        if( ( ( ui->comboBoxCodec->currentIndex() == CODEC_JPG2K )
             && ( arg1 != QString( "Movie (*.mov)" ) ) )
         || ( ui->comboBoxCodec->currentIndex() == CODEC_TIFF )
         || ( ui->comboBoxCodec->currentIndex() == CODEC_PNG ) )
        {
            ui->checkBoxFpsOverride->setEnabled( false );
            ui->checkBoxFpsOverride->setChecked( false );
        }
        else
        {
            ui->checkBoxFpsOverride->setEnabled( true );
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

//HDR toggled
void ExportSettingsDialog::on_checkBoxHdrBlending_toggled(bool checked)
{
    if( checked )
    {
        ui->comboBoxSmoothing->setCurrentIndex( 0 );
    }
    ui->comboBoxSmoothing->setEnabled( !checked );
    ui->label_Smoothing->setEnabled( !checked );
}

//Smooth aliasing changed
void ExportSettingsDialog::on_comboBoxSmoothing_currentIndexChanged(int index)
{
    if( index == 0 )
    {
        ui->checkBoxHdrBlending->setEnabled( true );
    }
    else
    {
        ui->checkBoxHdrBlending->setEnabled( false );
        ui->checkBoxHdrBlending->setChecked( false );
    }
}

//Add a new export preset
void ExportSettingsDialog::on_toolButtonAddPreset_clicked()
{
    QListWidgetItem *newItem = new QListWidgetItem( "New Preset" );
    newItem->setFlags( newItem->flags() | Qt::ItemIsEditable );
    ui->listWidget->addItem( newItem );
    qRegisterMetaTypeStreamOperators<QList<ExportPreset> >("QList<ExportPreset>");
    //Get existing list
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    QList<ExportPreset> presetList = set.value( "ExportPresets" ).value<QList<ExportPreset> >();
    //Build new item
    ExportPreset preset;
    preset.name = "New Preset";
    preset.currentCodecProfile = ui->comboBoxCodec->currentIndex();
    preset.currentCodecOption = ui->comboBoxOption->currentIndex();
    preset.debayerMode = ui->comboBoxDebayer->currentIndex();
    preset.resize = ui->checkBoxResize->isChecked();
    preset.resizeWidth = ui->spinBoxWidth->value();
    preset.resizeHeight = ui->spinBoxHeight->value();
    preset.fpsOverride = ui->checkBoxFpsOverride->isChecked();
    preset.fps = ui->doubleSpinBoxFps->value();
    preset.exportAudio = ui->checkBoxExportAudio->isChecked();
    preset.heightLocked = ui->toolButtonLockHeight->isChecked();
    preset.smooth = ui->comboBoxSmoothing->currentIndex();
    preset.scaleAlgo = 0; //not used
    preset.hdrBlending = ui->checkBoxHdrBlending->isChecked();
    //Save item + list
    presetList.append( preset );
    std::sort( presetList.begin(), presetList.end(), exportPresetLessThan );
    set.setValue( "ExportPresets", QVariant::fromValue(presetList) );
}

//Delete selected export preset
void ExportSettingsDialog::on_toolButtonDeletePreset_clicked()
{
    qRegisterMetaTypeStreamOperators<QList<ExportPreset> >("QList<ExportPreset>");
    //Get existing list
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    QList<ExportPreset> presetList = set.value( "ExportPresets" ).value<QList<ExportPreset> >();

    int currentItem = ui->listWidget->currentRow();
    if( currentItem >= presetList.count() ) return;
    if( currentItem < 0 ) return;

    presetList.removeAt( currentItem );
    set.setValue( "ExportPresets", QVariant::fromValue(presetList) );
    delete ui->listWidget->takeItem(currentItem);
}

//Rename, double clicked export preset
void ExportSettingsDialog::on_listWidget_itemChanged(QListWidgetItem *item)
{
    qRegisterMetaTypeStreamOperators<QList<ExportPreset> >("QList<ExportPreset>");
    //Get existing list
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    QList<ExportPreset> presetList = set.value( "ExportPresets" ).value<QList<ExportPreset> >();

    int currentItem = ui->listWidget->currentRow();
    if( currentItem >= presetList.count() ) return;
    if( currentItem < 0 ) return;

    ExportPreset preset = presetList.takeAt( currentItem );
    preset.name = item->text();
    presetList.insert( currentItem, preset );
    std::sort( presetList.begin(), presetList.end(), exportPresetLessThan );
    set.setValue( "ExportPresets", QVariant::fromValue(presetList) );
}

//Select export preset
void ExportSettingsDialog::on_listWidget_itemClicked(QListWidgetItem *item)
{
    Q_UNUSED( item );

    qRegisterMetaTypeStreamOperators<QList<ExportPreset> >("QList<ExportPreset>");
    //Get existing list
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    QList<ExportPreset> presetList = set.value( "ExportPresets" ).value<QList<ExportPreset> >();

    int currentItem = ui->listWidget->currentRow();
    if( currentItem >= presetList.count() ) return;
    if( currentItem < 0 ) return;

    ui->comboBoxCodec->setCurrentIndex( presetList.at(currentItem).currentCodecProfile );
    on_comboBoxCodec_currentIndexChanged( presetList.at(currentItem).currentCodecProfile );
    ui->comboBoxOption->setCurrentIndex( presetList.at(currentItem).currentCodecOption );
    ui->comboBoxDebayer->setCurrentIndex( presetList.at(currentItem).debayerMode );
    ui->checkBoxResize->setChecked( presetList.at(currentItem).resize );
    on_checkBoxResize_toggled( presetList.at(currentItem).resize );
    ui->spinBoxWidth->setValue( presetList.at(currentItem).resizeWidth );
    ui->spinBoxHeight->setValue( presetList.at(currentItem).resizeHeight );
    ui->checkBoxFpsOverride->setChecked( presetList.at(currentItem).fpsOverride );
    on_checkBoxFpsOverride_toggled( presetList.at(currentItem).fpsOverride );
    ui->doubleSpinBoxFps->setValue( presetList.at(currentItem).fps );
    ui->checkBoxExportAudio->setChecked( presetList.at(currentItem).exportAudio );
    ui->toolButtonLockHeight->setChecked( presetList.at(currentItem).heightLocked );
    ui->comboBoxSmoothing->setCurrentIndex( presetList.at(currentItem).smooth );
    //... presetList.at(currentItem).scaleAlgo ... //not used
    ui->checkBoxHdrBlending->setChecked( presetList.at(currentItem).hdrBlending );
}

//React also on arrow buttons
void ExportSettingsDialog::on_listWidget_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    if( m_blockOnce )
    {
        m_blockOnce = false;
        return;
    }
    if( current == previous ) return;
    on_listWidget_itemClicked( current );
}
