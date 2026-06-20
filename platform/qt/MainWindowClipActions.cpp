/*!
 * \file MainWindowClipActions.cpp
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

//Delete selected files from session
void MainWindow::deleteFileFromSession( void )
{
    //Save slider receipt
    setReceipt( ACTIVE_RECEIPT );

    //Ask for options
    QMessageBox msg;
    msg.setIcon( QMessageBox::Question );
    msg.setWindowTitle( tr( "%1 - Remove clip" ).arg( APPNAME ) );
    msg.setText( tr( "Remove clip from session, or delete clip from disk?" ) );
    msg.addButton(tr("Remove"), QMessageBox::ApplyRole);
    QPushButton *deleteButton = msg.addButton(tr("Delete from Disk"), QMessageBox::ActionRole);
    QPushButton *abortButton = msg.addButton(tr("Abort"), QMessageBox::RejectRole);
    msg.setDefaultButton( abortButton );
    msg.exec();
    if( msg.clickedButton() == abortButton ) return;

    //begin clip delete process
    m_inClipDeleteProcess = true;

    //Save the current active row for selection after deletion
    int currentRow = m_pProxyModel->mapFromSource( m_pModel->index( SESSION_ACTIVE_CLIP_ROW, 0, QModelIndex() ) ).row();

    //If multiple selection is on, we need to erase all selected items
    QModelIndexList list = selectedClipsList();
    for( int i = list.size(); i > 0; i-- )
    {
        //Do nothing for hidden clips
        if( ui->tableViewSession->isRowHidden( list.at(i-1).row() ) ) continue;

        int row = list.at( i - 1 ).data( ROLE_REALINDEX ).toInt();
        //Delete file from disk when wanted
        if( msg.clickedButton() == deleteButton )
        {
            //MLV
#ifdef Q_OS_WIN //On windows the file has to be closed before beeing able to move to trash
            m_fileLoaded = false;
            m_dontDraw = true;
            freeMlvObject( m_pMlvObject );
            m_pMlvObject = initMlvObject();
#endif
            if( MoveToTrash( GET_RECEIPT(row)->fileName() ) ) QMessageBox::critical( this, tr( "%1 - Delete clip from disk" ).arg( APPNAME ), tr( "Delete clip failed!" ) );
            //MAPP
            QString mappName = GET_RECEIPT(row)->fileName();
            mappName.chop( 4 );
            mappName.append( ".MAPP" );
            if( QFileInfo( mappName ).exists() )
            {
                if( MoveToTrash( mappName ) ) QMessageBox::critical( this, tr( "%1 - Delete MAPP file from disk" ).arg( APPNAME ), tr( "Delete MAPP file failed!" ) );
            }
            //M00..M99
            mappName.chop( 1 );
            for( int nr = 0; nr < 100; nr++ )
            {
                mappName.chop( 2 );
                mappName.append( QString( "%1" ).arg( nr, 2, 10, QChar( '0' ) ) );
                if( QFileInfo( mappName ).exists() )
                {
                    if( MoveToTrash( mappName ) ) QMessageBox::critical( this, tr( "%1 - Delete M%2 file from disk" ).arg( APPNAME ).arg( nr, 2, 10, QChar( '0' ) ), tr( "Delete M%1 file failed!" ).arg( nr, 2, 10, QChar( '0' ) ) );
                }
                else
                {
                    break;
                }
            }
        }
        int delrow = m_pProxyModel->mapFromSource( m_pModel->index( row, 0, QModelIndex() ) ).row();
        //Remove item from Session List & Remove slider memory
        m_pModel->removeRow( row, QModelIndex() );
        //influences actual loaded clip?
        if( currentRow > delrow ) currentRow--;
        if( currentRow < 0 ) currentRow = 0;
    }

    //if there is at least one...
    if( SESSION_CLIP_COUNT > 0 )
    {
        //Open the nearest clip from last opened!
        if( currentRow >= SESSION_CLIP_COUNT ) currentRow = SESSION_CLIP_COUNT - 1;
        if( currentRow < 0 ) currentRow = 0;
        SET_ACTIVE_CLIP_IDX( m_pProxyModel->index( currentRow, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt() );
        showFileInEditor( m_pProxyModel->index( currentRow, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt() );
        //m_pSelectionModel->setCurrentIndex( m_pProxyModel->mapFromSource( m_pModel->index( m_pProxyModel->index( currentRow, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt(), 0, QModelIndex() ) ), QItemSelectionModel::ClearAndSelect );
        //openMlv( ACTIVE_CLIP->getPath() );
        //setSliders( ACTIVE_RECEIPT, false );

        //Caching is in which state? Set it!
        if( ui->actionCaching->isChecked() ) on_actionCaching_triggered();
    }
    else
    {
        //All black
        deleteSession();
    }

    //End clip delete process
    m_inClipDeleteProcess = false;
}

//Rename the selected clip
void MainWindow::renameActiveClip( void )
{
    //Catch all invalid cases to not crash the app or damage clips
    if( !m_pModel || SESSION_EMPTY || m_pModel->activeRow() < 0 ) return;

    //Save slider receipt
    setReceipt( ACTIVE_RECEIPT );

    //If multiple selection is on, we do nothing. We just rename one selected clip
    QModelIndexList list = selectedClipsList();
    if( list.size() != 1 ) return;

    int row = list.first().data( ROLE_REALINDEX ).toInt();

    RenameDialog *rd = new RenameDialog( this, m_pModel->clip( row )->getName() );
    if( !rd->exec() )
    {
        delete rd;
        return;
    }
    QString newFileName = rd->clipName();
    delete rd;

    if( m_pModel->clip( row )->getName() == newFileName ) return;

    QString fileName = GET_RECEIPT(row)->fileName();
    QString newFilePath = QFileInfo( fileName ).path() + "/" + newFileName;

    //Unload clip for Windows
    freeMlvObject( m_pMlvObject );
    m_pMlvObject = initMlvObject();

    //MLV
    bool ok = QFile( fileName ).rename( newFilePath );
    //MAPP
    QString mappName = fileName;
    mappName.chop( 4 );
    mappName.append( ".MAPP" );
    QString newMappPath = newFilePath;
    newMappPath.chop( 4 );
    newMappPath.append( ".MAPP" );
    if( QFile( mappName ).exists() )
    {
        ok = ok && QFile( mappName ).rename( newMappPath );
    }
    //M00..M99
    mappName.chop( 1 );
    newMappPath.chop( 1 );
    for( int nr = 0; nr < 100; nr++ )
    {
        mappName.chop( 2 );
        newMappPath.chop( 2 );
        mappName.append( QString( "%1" ).arg( nr, 2, 10, QChar( '0' ) ) );
        newMappPath.append( QString( "%1" ).arg( nr, 2, 10, QChar( '0' ) ) );
        if( QFileInfo( mappName ).exists() )
        {
            ok = ok && QFile( mappName ).rename( newMappPath );
        }
        else
        {
            break;
        }
    }

    if( ok )
    {
        GET_RECEIPT(row)->setFileName( newFilePath );
        m_pModel->clip( row )->setPathName( newFileName, newFilePath );
    }
    else
    {
        QMessageBox::critical( this, tr( "Renaming clip" ).arg( APPNAME ), tr( "Renaming clip failed!" ) );
    }

    //Open the clip again without rendering
    openMlv( ACTIVE_CLIP->getPath() );
    m_frameChanged = false;
    setSliders( ACTIVE_RECEIPT, false );
}

//Shows the file, which is selected via contextmenu
void MainWindow::rightClickShowFile( void )
{
    showFileInEditor( selectedClipsList().first().row() );
}

//Select all files in SessionList
void MainWindow::selectAllFiles( void )
{
    if( m_previewMode == 4 ) ui->tableViewSession->selectAll();
    else ui->listViewSession->selectAll();
}

//Show selected file from session in OSX Finder
void MainWindow::on_actionShowInFinder_triggered( void )
{
    if( SESSION_CLIP_COUNT == 0 ) return;

    QString path = GET_RECEIPT( m_pProxyModel->mapToSource( m_pSelectionModel->currentIndex() ).row() )->fileName();

#ifdef _WIN32    //Code for Windows
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
#elif defined(__APPLE__)    //Code for Mac
    QProcess::execute("/usr/bin/osascript", {"-e", "tell application \"Finder\" to reveal POSIX file \"" + path + "\""});
    QProcess::execute("/usr/bin/osascript", {"-e", "tell application \"Finder\" to activate"});
#elif defined( Q_OS_LINUX )
    QProcess::startDetached("xdg-open", {QFileInfo(path).absolutePath()});
#endif
}

//Show selected file with external application
void MainWindow::on_actionOpenWithExternalApplication_triggered( void )
{
    if( SESSION_CLIP_COUNT == 0 ) return;

#ifdef Q_OS_OSX     //Code for OSX
    //First check -> select app if fail
    if( !QDir( m_externalApplicationName ).exists() || m_externalApplicationName.size() == 0 )
    {
        on_actionSelectExternalApplication_triggered();
    }
    //2nd check -> cancel if still fails
    if( !QDir( m_externalApplicationName ).exists() )
    {
        return;
    }
    //Now open
    QFileInfo info( m_externalApplicationName );
    QString path = info.fileName();
    if( path.endsWith( ".app" ) ) path = path.left( path.size() - 4 );
    QProcess::startDetached( QString( "open -a \"%1\" \"%2\"" )
                           .arg( path )
                           .arg( GET_RECEIPT( m_pProxyModel->mapToSource( m_pSelectionModel->currentIndex() ).row() )->fileName() ) );
#else    //Code for Windows & Linux
    //First check -> select app if fail
    if( !QFileInfo( m_externalApplicationName ).exists() ) on_actionSelectExternalApplication_triggered();
    //2nd check -> cancel if still fails
    if( !QFileInfo( m_externalApplicationName ).exists() ) return;
    //Now open
    QProcess::execute( QString( "%1" ).arg( m_externalApplicationName ), {QString( "%1" ).arg( QDir::toNativeSeparators( GET_RECEIPT( m_pProxyModel->mapToSource( m_pSelectionModel->currentIndex() ).row() )->fileName() ) ) } );
#endif
}

//Select the application for "Open with external application"
void MainWindow::on_actionSelectExternalApplication_triggered()
{
    QString path;
#ifdef _WIN32
    path = "C:\\";
    path = QFileDialog::getOpenFileName( this,
                 tr("Select external application"), path,
                 tr("Executable (*.exe)") );
    if( path.size() == 0 ) return;
#endif
#ifdef Q_OS_LINUX
    path = "/";
    path = QFileDialog::getOpenFileName( this,
                 tr("Select external application"), path,
                 tr("Application (*)") );
    if( path.size() == 0 ) return;
#endif
#ifdef Q_OS_OSX
    path = "/Applications/";
    path = QFileDialog::getOpenFileName( this,
                 tr("Select external application"), path,
                 tr("Application (*.app)") );
    if( path.size() == 0 ) return;
#endif
    m_externalApplicationName = path;
}
