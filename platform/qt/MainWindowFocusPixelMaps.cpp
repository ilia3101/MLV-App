/*!
 * \file MainWindowFocusPixelMaps.cpp
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

//Check if a focus pixel map is needed and installed, if not download and install it
void MainWindow::focusPixelCheckAndInstallation()
{
    if( llrpDetectFocusDotFixMode( m_pMlvObject ) != 0 )
    {
        FocusPixelMapManager *fpmManager = new FocusPixelMapManager( this );
        if( !fpmManager->isDownloaded( m_pMlvObject ) && fpmManager->isMapAvailable( m_pMlvObject ) )
        {
            //Camera name?
            uint32_t camId = getMlvCameraModel( m_pMlvObject );
            QString camName = QString( "%1" ).arg( camidGetCameraName( camId, 0 ) );
            if( camidGetCameraName( camId, 1 ) != NULL ) camName.append( QString( " / %1" ).arg( camidGetCameraName( camId, 1 ) ) );
            if( camidGetCameraName( camId, 2 ) != NULL ) camName.append( QString( " / %1" ).arg( camidGetCameraName( camId, 2 ) ) );

            QMessageBox msg;
            msg.setIcon( QMessageBox::Question );
            msg.setText( tr( "Download and install focus pixel map for this clip or install all focus pixel maps for %1?" ).arg( camName ) );
            QPushButton *singleButton = msg.addButton(tr("Single Map"), QMessageBox::ApplyRole);
            QPushButton *allButton = msg.addButton(tr("All Maps"), QMessageBox::ActionRole);
            msg.addButton(tr("None"), QMessageBox::RejectRole);
            msg.setDefaultButton( singleButton );
            msg.exec();

            StatusFpmDialog *status = new StatusFpmDialog( this );
            if( msg.clickedButton() == singleButton )
            {
                status->open();
                if( fpmManager->downloadMap( m_pMlvObject ) )
                {
                    //QMessageBox::information( this, APPNAME, tr( "Download and installation of focus pixel map successful." ) );
                    status->close();
                    showFileInEditor( SESSION_ACTIVE_CLIP_ROW );
                }
                else
                {
                    status->close();
                    QMessageBox::critical( this, APPNAME, tr( "Download and installation of focus pixel map failed." ) );
                }
            }
            else if( msg.clickedButton() == allButton )
            {
                status->open();
                if( fpmManager->downloadAllMaps( m_pMlvObject ) )
                {
                    //QMessageBox::information( this, APPNAME, tr( "Download and installation of focus pixel maps successful." ) );
                    status->close();
                    showFileInEditor( SESSION_ACTIVE_CLIP_ROW );
                }
                else
                {
                    status->close();
                    QMessageBox::critical( this, APPNAME, tr( "Download and installation of focus pixel maps failed." ) );
                }
            }
            delete status;

        }
        delete fpmManager;
    }
}

//Trigger check for FPM update
void MainWindow::checkFocusPixelUpdate()
{
    FocusPixelMapManager *manager = new FocusPixelMapManager( this );
    int updateFpm = manager->updateAllMaps( true );
    if( updateFpm > 0 )
    {
        if( QMessageBox::Yes == QMessageBox::information( this, APPNAME, tr( "Update available for %1 focus pixel map(s).\nDownload and install?" ).arg( updateFpm ), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel ) )
        {
            StatusFpmDialog *status = new StatusFpmDialog( this );
            status->open();
            int ret = manager->updateAllMaps( false );
            status->close();
            if( ret != updateFpm ) QMessageBox::critical( this, APPNAME, tr( "Update of focus pixel maps failed." ) );
        }
    }
    delete manager;
}
