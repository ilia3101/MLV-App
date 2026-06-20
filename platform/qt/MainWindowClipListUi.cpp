/*!
 * \file MainWindowClipListUi.cpp
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

//Jump to next clip
void MainWindow::on_actionNext_Clip_triggered()
{
    //int currentRow = m_pSelectionModel->currentIndex().row();
    int currentRow = m_pProxyModel->mapFromSource( m_pModel->index( SESSION_ACTIVE_CLIP_ROW, 0, QModelIndex() ) ).row();

    if( ( ( currentRow + 1 ) < SESSION_CLIP_COUNT ) && m_fileLoaded )
    {
        //Search the next visible clip, if any
        for( int i = currentRow + 1; i < SESSION_CLIP_COUNT; i++ )
        {
            if( !ui->listViewSession->isRowHidden( i ) )
            {
                showFileInEditor( m_pProxyModel->index( i, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt() );
                m_pSelectionModel->setCurrentIndex( m_pProxyModel->index( i, 0, QModelIndex() ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
                return;
            }
        }
    }
}

//Jump to previous clip
void MainWindow::on_actionPrevious_Clip_triggered()
{
    //int currentRow = m_pSelectionModel->currentIndex().row();
    int currentRow = m_pProxyModel->mapFromSource( m_pModel->index( SESSION_ACTIVE_CLIP_ROW, 0, QModelIndex() ) ).row();

    if( ( currentRow > 0 ) && m_fileLoaded )
    {
        //Search the previous visible clip, if any
        for( int i = currentRow - 1; i >= 0; i-- )
        {
            if( !ui->listViewSession->isRowHidden( i ) )
            {
                showFileInEditor( m_pProxyModel->index( i, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt() );
                m_pSelectionModel->setCurrentIndex( m_pProxyModel->index( i, 0, QModelIndex() ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
                return;
            }
        }
    }
}

//Select all clips via action
void MainWindow::on_actionSelectAllClips_triggered()
{
    if( SESSION_CLIP_COUNT > 0 )
    {
        selectAllFiles();
    }
}

//Delete clip from session via action
void MainWindow::on_actionDeleteSelectedClips_triggered()
{
    if( SESSION_CLIP_COUNT > 0 )
    {
        deleteFileFromSession();
        ui->actionDeleteSelectedClips->setEnabled( false );
    }
}

//FileName in SessionList doubleClicked
void MainWindow::on_listViewSession_activated(const QModelIndex &index)
{
    showFileInEditor( index.data( ROLE_REALINDEX ).toInt() );
}

//FileName in SessionTable doubleClicked
void MainWindow::on_tableViewSession_activated(const QModelIndex &index)
{
    showFileInEditor( index.data( ROLE_REALINDEX ).toInt() );
}

//Sessionlist visibility changed -> redraw picture
void MainWindow::on_dockWidgetSession_visibilityChanged(bool visible)
{
    if( !isMinimized() )
    {
        ui->actionShowSessionArea->setChecked( visible );
        qApp->processEvents();
        m_frameChanged = true;
    }
}

//Edit area visibility changed -> redraw picture
void MainWindow::on_dockWidgetEdit_visibilityChanged(bool visible)
{
    if( !isMinimized() )
    {
        ui->actionShowEditArea->setChecked( visible );
        qApp->processEvents();
        m_frameChanged = true;
    }
}

//Set visibility of audio track
void MainWindow::on_actionShowAudioTrack_toggled(bool checked)
{
    ui->labelAudioTrack->setVisible( checked );
    qApp->processEvents();
    m_frameChanged = true;
}

//Rightclick on SessionList
void MainWindow::on_listViewSession_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->listViewSession->mapToGlobal( pos );

    // Create mark menu
    QMenu markMenu;
    markMenu.addAction( ui->actionMarkRed );
    markMenu.addAction( ui->actionMarkYellow );
    markMenu.addAction( ui->actionMarkGreen );
    markMenu.addAction( ui->actionUnmark );

    // Create menu and insert some actions
    QMenu myMenu;
    QModelIndexList list = selectedClipsList();
    if( SESSION_CLIP_COUNT > 0 )
    {
        if( list.size() == 1 )
        {
            myMenu.addAction( ui->actionSelectAllClips );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Image-icon.png" ), "Show in Editor",  this, SLOT( rightClickShowFile() ) );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Delete-icon.png" ), "Delete Selected File from Session",  this, SLOT( deleteFileFromSession() ) );
            myMenu.addAction( ui->actionRename );
            markMenu.setTitle( "Mark Clip" );
            myMenu.addMenu( &markMenu );
            myMenu.addSeparator();
            myMenu.addAction( ui->actionShowInFinder );
            myMenu.addAction( ui->actionOpenWithExternalApplication );
            myMenu.addAction( ui->actionSelectExternalApplication );
            myMenu.addSeparator();
        }
        else if( list.size() > 1 )
        {
            myMenu.addAction( ui->actionPasteReceipt );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Delete-icon.png" ), "Delete Selected Files from Session",  this, SLOT( deleteFileFromSession() ) );
            markMenu.setTitle( "Mark Clips" );
            myMenu.addMenu( &markMenu );
            myMenu.addSeparator();
        }
    }
    myMenu.addMenu( ui->menuSessionListPreview );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Rightclick on SessionTable
void MainWindow::on_tableViewSession_customContextMenuRequested(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = ui->listViewSession->mapToGlobal( pos );

    // Create mark menu
    QMenu markMenu;
    markMenu.addAction( ui->actionMarkRed );
    markMenu.addAction( ui->actionMarkYellow );
    markMenu.addAction( ui->actionMarkGreen );
    markMenu.addAction( ui->actionUnmark );

    // Create menu and insert some actions
    QMenu myMenu;
    QModelIndexList list = selectedClipsList();
    if( SESSION_CLIP_COUNT > 0 )
    {
        if( list.size() == 1 )
        {
            myMenu.addAction( ui->actionSelectAllClips );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Image-icon.png" ), "Show in Editor",  this, SLOT( rightClickShowFile() ) );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Delete-icon.png" ), "Delete Selected File from Session",  this, SLOT( deleteFileFromSession() ) );
            myMenu.addAction( "Rename", this, SLOT( renameActiveClip() ) );
            markMenu.setTitle( "Mark Clip" );
            myMenu.addMenu( &markMenu );
            myMenu.addSeparator();
            myMenu.addAction( ui->actionShowInFinder );
            myMenu.addAction( ui->actionOpenWithExternalApplication );
            myMenu.addAction( ui->actionSelectExternalApplication );
            myMenu.addSeparator();
        }
        else if( list.size() > 1 )
        {
            myMenu.addAction( ui->actionPasteReceipt );
            myMenu.addAction( QIcon( ":/RetinaIMG/RetinaIMG/Delete-icon.png" ), "Delete Selected Files from Session",  this, SLOT( deleteFileFromSession() ) );
            markMenu.setTitle( "Mark Clips" );
            myMenu.addMenu( &markMenu );
            myMenu.addSeparator();
        }
    }
    myMenu.addMenu( ui->menuSessionListPreview );
    // Show context menu at handling position
    myMenu.exec( globalPos );
}

//Mark selected clips Red
void MainWindow::on_actionMarkRed_triggered()
{
    QModelIndexList list = selectedClipsList();
    for( int i = 0; i < list.size(); i++ )
    {
        //Do nothing for hidden clips
        if( ui->tableViewSession->isRowHidden( list.at( i ).row() ) ) continue;

        int row = list.at( i ).data( ROLE_REALINDEX ).toInt();
        GET_RECEIPT( row )->setMark( 1 );
        setMarkColor( row, 1 );
    }
}

//Mark selected clips Yellow
void MainWindow::on_actionMarkYellow_triggered()
{
    QModelIndexList list = selectedClipsList();
    for( int i = 0; i < list.size(); i++ )
    {
        //Do nothing for hidden clips
        if( ui->tableViewSession->isRowHidden( list.at( i ).row() ) ) continue;

        int row = list.at( i ).data( ROLE_REALINDEX ).toInt();
        GET_RECEIPT( row )->setMark( 2 );
        setMarkColor( row, 2 );
    }
}

//Mark selected clips Green
void MainWindow::on_actionMarkGreen_triggered()
{
    QModelIndexList list = selectedClipsList();
    for( int i = 0; i < list.size(); i++ )
    {
        //Do nothing for hidden clips
        if( ui->tableViewSession->isRowHidden( list.at( i ).row() ) ) continue;

        int row = list.at( i ).data( ROLE_REALINDEX ).toInt();
        GET_RECEIPT( row )->setMark( 3 );
        setMarkColor( row, 3 );
    }
}

//Unmark selected clips
void MainWindow::on_actionUnmark_triggered()
{
    QModelIndexList list = selectedClipsList();
    for( int i = 0; i < list.size(); i++ )
    {
        //Do nothing for hidden clips
        if( ui->tableViewSession->isRowHidden( list.at( i ).row() ) ) continue;

        int row = list.at( i ).data( ROLE_REALINDEX ).toInt();
        GET_RECEIPT( row )->setMark( 0 );
        setMarkColor( row, 0 );
    }
}

//Show the red clips, or not
void MainWindow::on_actionShowRedClips_toggled(bool arg1)
{
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        int realIndex = m_pProxyModel->index( i, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt();
        if( GET_RECEIPT( realIndex )->mark() == 1 )
        {
            ui->listViewSession->setRowHidden( i, !arg1 );
            ui->tableViewSession->setRowHidden( i, !arg1 );
        }
    }
}

//Show the yellow clips, or not
void MainWindow::on_actionShowYellowClips_toggled(bool arg1)
{
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        int realIndex = m_pProxyModel->index( i, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt();
        if( GET_RECEIPT( realIndex )->mark() == 2 )
        {
            ui->listViewSession->setRowHidden( i, !arg1 );
            ui->tableViewSession->setRowHidden( i, !arg1 );
        }
    }
}

//Show the green clips, or not
void MainWindow::on_actionShowGreenClips_toggled(bool arg1)
{
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        int realIndex = m_pProxyModel->index( i, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt();
        if( GET_RECEIPT( realIndex )->mark() == 3 )
        {
            ui->listViewSession->setRowHidden( i, !arg1 );
            ui->tableViewSession->setRowHidden( i, !arg1 );
        }
    }
}

//Show the unmarked clips, or not
void MainWindow::on_actionShowUnmarkedClips_toggled(bool arg1)
{
    for( int i = 0; i < SESSION_CLIP_COUNT; i++ )
    {
        int realIndex = m_pProxyModel->index( i, 0, QModelIndex() ).data( ROLE_REALINDEX ).toInt();
        if( GET_RECEIPT( realIndex )->mark() == 0 )
        {
            ui->listViewSession->setRowHidden( i, !arg1 );
            ui->tableViewSession->setRowHidden( i, !arg1 );
        }
    }
}

//Mark clipNr with color
void MainWindow::setMarkColor(int clipNr, uint8_t mark)
{
    int listOrTableRow = m_pProxyModel->mapFromSource( m_pModel->index( clipNr, 0, QModelIndex() ) ).row();

    if( mark == 1 )
    {
        GET_CLIP(clipNr)->setBackgroundColor( QColor( 255, 0, 0, 80 ) );
        ui->listViewSession->setRowHidden( listOrTableRow, !ui->actionShowRedClips->isChecked() );
        ui->tableViewSession->setRowHidden( listOrTableRow, !ui->actionShowRedClips->isChecked() );
    }
    else if( mark == 2 )
    {
        GET_CLIP(clipNr)->setBackgroundColor( QColor( 255, 255, 0, 80 ) );
        ui->listViewSession->setRowHidden( listOrTableRow, !ui->actionShowYellowClips->isChecked() );
        ui->tableViewSession->setRowHidden( listOrTableRow, !ui->actionShowYellowClips->isChecked() );
    }
    else if( mark == 3 )
    {
        GET_CLIP(clipNr)->setBackgroundColor( QColor( 0, 255, 0, 80 ) );
        ui->listViewSession->setRowHidden( listOrTableRow, !ui->actionShowGreenClips->isChecked() );
        ui->tableViewSession->setRowHidden( listOrTableRow, !ui->actionShowGreenClips->isChecked() );
    }
    else
    {
        GET_CLIP(clipNr)->setBackgroundColor( QColor( 0, 0, 0, 0 ) );
        ui->listViewSession->setRowHidden( listOrTableRow, !ui->actionShowUnmarkedClips->isChecked() );
        ui->tableViewSession->setRowHidden( listOrTableRow, !ui->actionShowUnmarkedClips->isChecked() );
    }
}

//Create a list of selected clips (items from first column)
QModelIndexList MainWindow::selectedClipsList()
{
    QModelIndexList list;
    for( int i = 0; i < m_pSelectionModel->selectedIndexes().size(); i++ )
    {
        if( m_pSelectionModel->selectedIndexes().at(i).column() != 0 ) continue;
        list.append( m_pSelectionModel->selectedIndexes().at(i) );
    }
    return list;
}
