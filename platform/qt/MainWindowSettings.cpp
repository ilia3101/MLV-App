/*!
 * \file MainWindowSettings.cpp
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

//Read some settings from registry
void MainWindow::readSettings()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    restoreGeometry( set.value( "mainWindowGeometry" ).toByteArray() );
    //restoreState( set.value( "mainWindowState" ).toByteArray() ); // create docks, toolbars, etc...
    if( set.value( "dragFrameMode", true ).toBool() ) ui->actionDropFrameMode->setChecked( true );
    if( set.value( "audioOutput", true ).toBool() ) ui->actionAudioOutput->setChecked( true );
    if( set.value( "zebras", false ).toBool() ) ui->actionShowZebras->setChecked( true );
    ui->actionFastOpen->setChecked( set.value( "fastOpen", true ).toBool() );
    m_lastExportPath = set.value( "lastExportPath", QDir::homePath() ).toString();
    m_lastMlvOpenFileName = set.value( "lastMlvFileName", QDir::homePath() ).toString();
    m_lastSessionFileName = set.value( "lastSessionFileName", QDir::homePath() ).toString();
    m_lastReceiptFileName = set.value( "lastReceiptFileName", QDir::homePath() ).toString();
    m_lastDarkframeFileName = set.value( "lastDarkframeFileName", QDir::homePath() ).toString();
    m_externalApplicationName = set.value( "externalAppName", QString( "" ) ).toString();
    m_lastLutFileName = set.value( "lastLutFile", QDir::homePath() ).toString();
    m_codecProfile = set.value( "codecProfile", 4 ).toUInt();
    m_codecOption = set.value( "codecOption", 0 ).toUInt();
    m_exportDebayerMode = set.value( "exportDebayerMode", 4 ).toUInt();
    m_previewMode = set.value( "previewMode", 1 ).toUInt();
    switch( m_previewMode )
    {
    case 0:
        ui->actionPreviewDisabled->setChecked( true );
        on_actionPreviewDisabled_triggered();
        break;
    case 1:
        ui->actionPreviewList->setChecked( true );
        on_actionPreviewList_triggered();
        break;
    case 2:
        ui->actionPreviewPicture->setChecked( true );
        on_actionPreviewPicture_triggered();
        break;
    case 3:
        ui->actionPreviewPictureBottom->setChecked( true );
        on_actionPreviewPictureBottom_triggered();
        break;
    default:
        ui->actionPreviewTableModeBottom->setChecked( true );
        on_actionPreviewTableModeBottom_triggered();
        break;
    }
    ui->actionCaching->setChecked( false );
    m_resizeFilterEnabled = set.value( "resizeEnable", false ).toBool();
    m_resizeWidth = set.value( "resizeWidth", 1920 ).toUInt();
    m_resizeHeight = set.value( "resizeHeight", 1080 ).toUInt();
    m_resizeFilterHeightLocked = set.value( "resizeLockHeight", false ).toBool();
    m_smoothFilterSetting = set.value( "smoothEnabled", 0 ).toUInt();
    m_hdrExport = set.value( "hdrExport", false ).toBool();
    m_fpsOverride = set.value( "fpsOverride", false ).toBool();
    m_frameRate = set.value( "frameRate", 25 ).toDouble();
    m_audioExportEnabled = set.value( "audioExportEnabled", true ).toBool();
    ui->groupBoxRawCorrection->setChecked( set.value( "expandedRawCorrection", false ).toBool() );
    ui->groupBoxCutInOut->setChecked( set.value( "expandedCutInOut", false ).toBool() );
    ui->groupBoxDebayer->setChecked( set.value( "expandedDebayer", true ).toBool() );
    ui->groupBoxProfiles->setChecked( set.value( "expandedProfiles", true ).toBool() );
    ui->groupBoxProcessing->setChecked( set.value( "expandedProcessing", true ).toBool() );
    ui->groupBoxDetails->setChecked( set.value( "expandedDetails", false ).toBool() );
    ui->groupBoxHsl->setChecked( set.value( "expandedHsl", false ).toBool() );
    ui->groupBoxToning->setChecked( set.value( "expandedToning", false ).toBool() );
    ui->groupBoxColorWheels->setChecked( set.value( "expandedColorWheels", false ).toBool() );
    ui->groupBoxLut->setChecked( set.value( "expandedLut", false ).toBool() );
    ui->groupBoxFilter->setChecked( set.value( "expandedFilter", false ).toBool() );
    ui->groupBoxVignette->setChecked( set.value( "expandedVignette", false ).toBool() );
    ui->groupBoxLinearGradient->setChecked( set.value( "expandedLinGradient", false ).toBool() );
    ui->groupBoxTransformation->setChecked( set.value( "expandedTransformation", false ).toBool() );
    ui->actionCreateMappFiles->setChecked( set.value( "createMappFiles", false ).toBool() );
    m_timeCodePosition = set.value( "tcPos", 1 ).toUInt();
    ui->actionAutoCheckForUpdates->setChecked( set.value( "autoUpdateCheck", true ).toBool() );
    ui->actionPlaybackPosition->setChecked( set.value( "rememberPlaybackPos", false ).toBool() );
    resizeDocks({ui->dockWidgetEdit}, {set.value( "dockEditSize", 212 ).toInt()}, Qt::Horizontal);
    resizeDocks({ui->dockWidgetSession}, {set.value( "dockSessionSize", 170 ).toInt()}, Qt::Horizontal);
    resizeDocks({ui->dockWidgetSession}, {set.value( "dockSessionSize", 130 ).toInt()}, Qt::Vertical);
    m_pRecentFilesMenu->restoreState( set.value("recentSessions").toByteArray() );
    ui->actionAskForSavingOnQuit->setChecked( set.value( "askForSavingOnQuit", true ).toBool() );
    ui->actionNotificationExportFinished->setChecked( set.value( "notificationExportFinished", true ).toBool() );
    ui->actionBetterResizer->setChecked( set.value( "betterResizerViewer", false ).toBool() );
    m_defaultReceiptFileName = set.value( "defaultReceiptFileName", QDir::homePath() ).toString();
    ui->actionUseDefaultReceipt->setChecked( set.value( "defaultReceiptEnabled", false ).toBool() );
    int themeId = set.value( "themeId", 0 ).toInt();
    if( themeId == 0 )
    {
        ui->actionDarkThemeStandard->setChecked( true );
        on_actionDarkThemeStandard_triggered( true );
    }
    else
    {
        ui->actionDarkThemeModern->setChecked( true );
        on_actionDarkThemeModern_triggered( true );
    }
    ui->graphicsView->setBackgroundBrush( QBrush( QColor( set.value( "backgroundcolorR", 0 ).toUInt(),
                                                          set.value( "backgroundcolorG", 0 ).toUInt(),
                                                          set.value( "backgroundcolorB", 0 ).toUInt() ), Qt::SolidPattern ) );
}

//Save some settings to registry
void MainWindow::writeSettings()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.setValue( "mainWindowGeometry", saveGeometry() );
    //set.setValue( "mainWindowState", saveState() ); // docks, toolbars, etc...
    set.setValue( "dragFrameMode", ui->actionDropFrameMode->isChecked() );
    set.setValue( "audioOutput", ui->actionAudioOutput->isChecked() );
    set.setValue( "zebras", ui->actionShowZebras->isChecked() );
    set.setValue( "fastOpen", ui->actionFastOpen->isChecked() );
    set.setValue( "lastExportPath", m_lastExportPath );
    set.setValue( "lastMlvFileName", m_lastMlvOpenFileName );
    set.setValue( "lastSessionFileName", m_lastSessionFileName );
    set.setValue( "lastReceiptFileName", m_lastReceiptFileName );
    set.setValue( "lastDarkframeFileName", m_lastDarkframeFileName );
    set.setValue( "externalAppName", m_externalApplicationName );
    set.setValue( "lastLutFile", m_lastLutFileName );
    set.setValue( "codecProfile", m_codecProfile );
    set.setValue( "codecOption", m_codecOption );
    set.setValue( "exportDebayerMode", m_exportDebayerMode );
    set.setValue( "previewMode", m_previewMode );
    set.setValue( "caching", ui->actionCaching->isChecked() );
    set.setValue( "resizeEnable", m_resizeFilterEnabled );
    set.setValue( "resizeWidth", m_resizeWidth );
    set.setValue( "resizeHeight", m_resizeHeight );
    set.setValue( "resizeLockHeight", m_resizeFilterHeightLocked );
    set.setValue( "smoothEnabled", m_smoothFilterSetting );
    set.setValue( "hdrExport", m_hdrExport );
    set.setValue( "fpsOverride", m_fpsOverride );
    set.setValue( "frameRate", m_frameRate );
    set.setValue( "audioExportEnabled", m_audioExportEnabled );
    set.setValue( "expandedRawCorrection", ui->groupBoxRawCorrection->isChecked() );
    set.setValue( "expandedCutInOut", ui->groupBoxCutInOut->isChecked() );
    set.setValue( "expandedDebayer", ui->groupBoxDebayer->isChecked() );
    set.setValue( "expandedProfiles", ui->groupBoxProfiles->isChecked() );
    set.setValue( "expandedProcessing", ui->groupBoxProcessing->isChecked() );
    set.setValue( "expandedDetails", ui->groupBoxDetails->isChecked() );
    set.setValue( "expandedHsl", ui->groupBoxHsl->isChecked() );
    set.setValue( "expandedToning", ui->groupBoxToning->isChecked() );
    set.setValue( "expandedColorWheels", ui->groupBoxColorWheels->isChecked() );
    set.setValue( "expandedLut", ui->groupBoxLut->isChecked() );
    set.setValue( "expandedFilter", ui->groupBoxFilter->isChecked() );
    set.setValue( "expandedVignette", ui->groupBoxVignette->isChecked() );
    set.setValue( "expandedLinGradient", ui->groupBoxLinearGradient->isChecked() );
    set.setValue( "expandedTransformation", ui->groupBoxTransformation->isChecked() );
    set.setValue( "createMappFiles", ui->actionCreateMappFiles->isChecked() );
    set.setValue( "tcPos", m_timeCodePosition );
    set.setValue( "autoUpdateCheck", ui->actionAutoCheckForUpdates->isChecked() );
    set.setValue( "rememberPlaybackPos", ui->actionPlaybackPosition->isChecked() );
    set.setValue( "dockEditSize", ui->dockWidgetEdit->width() );
    set.setValue( "defaultReceiptFileName", m_defaultReceiptFileName );
    set.setValue( "defaultReceiptEnabled", ui->actionUseDefaultReceipt->isChecked() );
    if( m_previewMode == 3 || m_previewMode == 4 ) set.setValue( "dockSessionSize", ui->dockWidgetSession->height() );
    else set.setValue( "dockSessionSize", ui->dockWidgetSession->width() );
    set.setValue( "recentSessions", m_pRecentFilesMenu->saveState() );
    set.setValue( "askForSavingOnQuit", ui->actionAskForSavingOnQuit->isChecked() );
    set.setValue( "notificationExportFinished", ui->actionNotificationExportFinished->isChecked() );
    set.setValue( "betterResizerViewer", ui->actionBetterResizer->isChecked() );
    if( ui->actionDarkThemeStandard->isChecked() ) set.setValue( "themeId", 0 );
    else set.setValue( "themeId", 1 );
    QColor backgroundColor = ui->graphicsView->backgroundBrush().color();
    set.setValue( "backgroundcolorR", backgroundColor.red() );
    set.setValue( "backgroundcolorG", backgroundColor.green() );
    set.setValue( "backgroundcolorB", backgroundColor.blue() );
}
