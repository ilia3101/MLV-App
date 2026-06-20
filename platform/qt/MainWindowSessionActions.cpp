/*!
 * \file MainWindowSessionActions.cpp
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

//New Session
void MainWindow::on_actionNewSession_triggered()
{
    if( SESSION_CLIP_COUNT && askToSaveCurrentSession() ) return;
    deleteSession();
}

//Open Session
void MainWindow::on_actionOpenSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSessionFileName ).absolutePath();
    if( !QDir( path ).exists() ) path = QDir::homePath();

    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open MLV App Session Xml"), path,
                                           tr("MLV App Session Xml files (*.masxml)"));

    //Abort selected
    if( fileName.size() == 0 ) return;

    if( SESSION_CLIP_COUNT && askToSaveCurrentSession() ) return;

    m_inOpeningProcess = true;
    openSession( fileName );
    //Show last imported file
    showFileInEditor( SESSION_CLIP_COUNT - 1 );
    m_sessionFileName = fileName;
    m_lastSessionFileName = fileName;
    m_inOpeningProcess = false;
    selectDebayerAlgorithm();
}

//Save Session (just save)
void MainWindow::on_actionSaveSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    if( m_sessionFileName.size() == 0 ) on_actionSaveAsSession_triggered();
    else saveSession( m_sessionFileName );
}

//Save Session with filename selection
void MainWindow::on_actionSaveAsSession_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSessionFileName ).absolutePath();
    QString fileName = QFileDialog::getSaveFileName(this,
                                           tr("Save MLV App Session Xml"), path,
                                           tr("MLV App Session Xml files (*.masxml)"));

    //Abort selected
    if( fileName.size() == 0 ) return;

    //Add ending, if it got lost using some OS...
    if( !fileName.endsWith( ".masxml" ) ) fileName.append( ".masxml" );

    m_sessionFileName = fileName;
    m_lastSessionFileName = fileName;

    saveSession( fileName );
}

//Export a csv table of session clips metadata
void MainWindow::on_actionSaveSessionMetadata_triggered()
{
    //Stop playback if active
    ui->actionPlay->setChecked( false );

    QString path = QFileInfo( m_lastSessionFileName ).absolutePath();
    QString fileName = QFileDialog::getSaveFileName(this,
                                           tr("Save MLV App Session Metadata"), path,
                                           tr("CSV (*.csv)"));

    //Abort selected
    if( fileName.size() == 0 ) return;

    //Add ending, if it got lost using some OS...
    if( !fileName.endsWith( ".csv" ) ) fileName.append( ".csv" );

    //Write file
    m_pModel->writeMetadataToCsv( fileName );
}
