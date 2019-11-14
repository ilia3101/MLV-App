/*!
 * \file TranscodeDialog.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Transcode any RAW to MLV and import to session
 */

#include "TranscodeDialog.h"
#include "ui_TranscodeDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QDebug>
#include <QProcess>

//Constructor
TranscodeDialog::TranscodeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TranscodeDialog)
{
    ui->setupUi(this);
    m_importList.clear();
    ui->treeWidget->hideColumn( 1 );
    ui->treeWidget->setColumnWidth( 0, 200 );
}

//Destructor
TranscodeDialog::~TranscodeDialog()
{
    delete ui;
}

//Get the transcoded file list for importing
QStringList TranscodeDialog::importList()
{
    return m_importList;
}

//Add single pictures for sinlge MLVs
void TranscodeDialog::on_pushButtonAddPics_clicked()
{
    QString path = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames( this, tr("Open one or more RAW files..."),
                                                    path,
                                                    tr("RAW files (*.*)") );

    if( files.empty() ) return;

    foreach( QString file, files )
    {
        QString fileName = QFileInfo( file ).fileName();

        QTreeWidgetItem *pItem = new QTreeWidgetItem( ui->treeWidget );
        pItem->setText( 0, fileName );
        pItem->setText( 1, file );
        fileName = fileName.left( fileName.lastIndexOf(".") ).append( ".MLV" );
        pItem->setText( 2, fileName );
    }
}

//Add folders with frames for single MLVs
void TranscodeDialog::on_pushButtonAddSequence_clicked()
{
    QString pathStart = QDir::homePath();
    QString folder = QFileDialog::getExistingDirectory(this, tr("Open RAW Sequences"),
                                                            pathStart,
                                                            QFileDialog::ShowDirsOnly
                                                            | QFileDialog::DontResolveSymlinks);

    if( !folder.count() ) return;

    QString path = folder.right( folder.count() - folder.lastIndexOf( "/" ) - 1 );

    QTreeWidgetItem *pItem = new QTreeWidgetItem( ui->treeWidget );
    pItem->setText( 0, path );
    //pItem->setText( 1, file );
    path.append( ".MLV" );
    pItem->setText( 2, path );

    QStringList list = QDir( folder ).entryList( QStringList() << "*.*", QDir::Files );
    foreach( QString file, list )
    {
        QTreeWidgetItem *pChild = new QTreeWidgetItem( pItem );
        QString fileName = QFileInfo( file ).fileName();
        pChild->setText( 0, fileName );
        pChild->setText( 1, QString( "%1/%2" ).arg( folder ).arg( file ) );
    }
}

//Transcode via RAW2MLV
void TranscodeDialog::on_pushButtonTranscode_clicked()
{
    QString path = QDir::homePath();
    QString folderName = QFileDialog::getExistingDirectory(this, tr("Choose Destination Folder"),
                                                      path,
                                                      QFileDialog::ShowDirsOnly
                                                      | QFileDialog::DontResolveSymlinks);
    if( folderName.length() == 0 ) return;

    m_importList.clear();

    //For all clips
    for( int i = 0; i < ui->treeWidget->topLevelItemCount(); i++ )
    {
#if defined __linux__ && !defined APP_IMAGE
        QString command = QString( "raw2mlv" );
#elif __WIN32__
        QString command = QString( "raw2mlv" );
#else
        QString command = QCoreApplication::applicationDirPath();
        command.append( QString( "/raw2mlv\"" ) );
        command.prepend( QString( "\"" ) );
#endif
        //Input
        if( ui->treeWidget->topLevelItem(i)->childCount() == 0 )
        {
            command.append( QString( " \"%1\"" ).arg( ui->treeWidget->topLevelItem(i)->text(1) ) );
        }
        else
        {
            for( int j = 0; j < ui->treeWidget->topLevelItem(i)->childCount(); j++ )
            {
                command.append( QString( " \"%1\"" ).arg( ui->treeWidget->topLevelItem(i)->child(j)->text(1) ) );
            }
        }

        //Output
        command.append( QString( " -o \"%1/%2\"" ).arg( folderName ).arg( ui->treeWidget->topLevelItem(i)->text(2) ) );

        qDebug() << command;
        QProcess process;
        process.execute( command );

        m_importList.append( QString( "%1/%2" ).arg( folderName ).arg( ui->treeWidget->topLevelItem(i)->text(2) ) );
    }
    close();
}
