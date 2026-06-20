/*!
 * \file MainWindowHelpActions.cpp
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

//About Window
void MainWindow::on_actionAbout_triggered()
{
    QPixmap pixmap = QPixmap( ":/IMG/IMG/Magic_Lantern_logo.png" )
                .scaled( 128 * devicePixelRatio(), 112 * devicePixelRatio(),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation );
        pixmap.setDevicePixelRatio( devicePixelRatio() );
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        pixmap.save(&buffer, "PNG");
        QString pic = QString("<img width='128' height='112' align='right' src=\"data:image/png;base64,") + byteArray.toBase64() + "\"/>";

        QMessageBox::about( this, QString( "About %1" ).arg( APPNAME ),
                                  QString(
                                    "<html>%1"
                                    "<body><h3>%2</h3>"
                                    " <p>%2 v%3</p>"
                                    " <p>%4</p>"
                                    " <p>See <a href='%5'>this site</a> for more information.</p>"
                                    " <p>Darkstyle Copyright (c) 2017, <a href='%6'>Juergen Skrotzky</a> under MIT</p>"
                                    " <p>Some icons by <a href='%7'>Double-J Design</a> under <a href='%8'>CC4.0</a></p>"
                                    " <p>Zhang-Wu LMMSE Image Demosaicking by Pascal Getreuer under <a href='%9'>BSD</a>.</p>"
                                    " <p>QRecentFilesMenu Copyright (c) 2011 by Morgan Leborgne under <a href='%10'>MIT</a>.</p>"
                                    " <p>Recursive bilateral filtering developed by Qingxiong Yang under <a href='%11'>MIT</a> and Ming under <a href='%12'>MIT</a>.</p>"
                                    " <p>AVIR image resizing algorithm designed by Aleksey Vaneev under <a href='%13'>MIT</a>.</p>"
                                    " <p>Sobel filter Copyright 2018 Pedro Melgueira under <a href='%14'>Apache 2.0</a>.</p>"
                                    " <p>maddy Markdown to HTML library under <a href='%15'>MIT</a>.</p>"
                                    " </body></html>" )
                                   .arg( pic ) //1
                                   .arg( APPNAME ) //2
                                   .arg( VERSION ) //3
                                   .arg( "by Ilia3101, bouncyball, Danne, dfort, orfeas-a, tlenke, fijha & masc." ) //4
                                   .arg( "https://github.com/ilia3101/MLV-App" ) //5
                                   .arg( "https://github.com/Jorgen-VikingGod" ) //6
                                   .arg( "http://www.doublejdesign.co.uk/" ) //7
                                   .arg( "https://creativecommons.org/licenses/by/4.0/" ) //8
                                   .arg( "http://www.opensource.org/licenses/bsd-license.html" ) //9
                                   .arg( "https://github.com/mojocorp/QRecentFilesMenu/blob/master/LICENSE" ) //10
                                   .arg( "https://github.com/ufoym/recursive-bf/blob/master/LICENSE" ) //11
                                   .arg( "https://github.com/Fig1024/OP_RBF/blob/master/LICENSE" ) //12
                                   .arg( "https://github.com/avaneev/avir/blob/master/LICENSE" ) //13
                                   .arg( "https://github.com/petermlm/SobelFilter/blob/master/LICENSE" ) //14
                                   .arg( "https://github.com/progsource/maddy/blob/master/LICENSE" ) ); //15
}

//Qt Infobox
void MainWindow::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt( this );
}

//Open UserManualDialog
void MainWindow::on_actionHelp_triggered()
{
    UserManualDialog *help = new UserManualDialog( this );
    help->exec();
    delete help;
}
