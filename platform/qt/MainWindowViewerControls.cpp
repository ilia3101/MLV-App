/*!
 * \file MainWindowViewerControls.cpp
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

//Contextmenu on picture
void MainWindow::pictureCustomContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->graphicsView->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionZoomFit );
    myMenu.addAction( ui->actionZoom100 );
    myMenu.addSeparator();
    myMenu.addMenu( ui->menuDemosaicForPlayback );
    myMenu.addAction( ui->actionBetterResizer );
    myMenu.addAction( ui->actionViewerBackgroundColor );
    myMenu.addSeparator();
    myMenu.addAction( ui->actionShowZebras );
    if( ui->actionFullscreen->isChecked() )
    {
        myMenu.addSeparator();
        myMenu.addAction( ui->actionGoto_First_Frame );
        myMenu.addAction( ui->actionPreviousFrame );
        myMenu.addAction( ui->actionPlay );
        myMenu.addAction( ui->actionNextFrame );
        myMenu.addAction( ui->actionLoop );
        myMenu.addSeparator();
        myMenu.addAction( ui->actionFullscreen );
    }
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Selection of gradation curve
void MainWindow::toolButtonGCurvesChanged( void )
{
    if( toolButtonGCurvesCurrentIndex() == 0 ) ui->labelCurves->setActiveLine( LINENR_W );
    else if( toolButtonGCurvesCurrentIndex() == 1 ) ui->labelCurves->setActiveLine( LINENR_R );
    else if( toolButtonGCurvesCurrentIndex() == 2 ) ui->labelCurves->setActiveLine( LINENR_G );
    else ui->labelCurves->setActiveLine( LINENR_B );
}

//Timecode label rightclick
void MainWindow::mpTcLabel_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = m_pTcLabel->mapToGlobal( pos );

    // Create menu and insert some actions
    QMenu myMenu;
    myMenu.addAction( ui->actionTimecodePositionMiddle );
    myMenu.addAction( ui->actionTimecodePositionRight );
    myMenu.addSeparator();
    myMenu.addAction( ui->actionToggleTimecodeDisplay );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//TimeCode label doubleclicked
void MainWindow::tcLabelDoubleClicked()
{
    m_tcModeDuration = !m_tcModeDuration;
    m_pTimeCodeImage->setTimeDurationMode( m_tcModeDuration );

    if( !m_fileLoaded )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( 0, 25 ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
        return;
    }

    //Refresh Timecode Label
    if( m_tcModeDuration )
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->spinBoxCutOut->value() - ui->spinBoxCutIn->value() + 1, getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
    else
    {
        QPixmap pic = QPixmap::fromImage( m_pTimeCodeImage->getTimeCodeLabel( ui->horizontalSliderPosition->value(), getFramerate() ).scaled( 200 * devicePixelRatio(),
                                                                                          30 * devicePixelRatio(),
                                                                                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
        pic.setDevicePixelRatio( devicePixelRatio() );
        m_pTcLabel->setPixmap( pic );
    }
}
