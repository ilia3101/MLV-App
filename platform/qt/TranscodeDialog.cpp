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
#include <QSettings>

//Constructor
TranscodeDialog::TranscodeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TranscodeDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );

    m_importList.clear();
    ui->treeWidget->hideColumn( 1 );
    ui->treeWidget->setColumnWidth( 0, 200 );

    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    m_lastSourcePath = set.value( "lastTranscodeSourcePath", QDir::homePath() ).toString();
    m_lastTargetPath = set.value( "lastTranscodeTargetPath", QDir::homePath() ).toString();

    ui->pushButtonTranscode->setEnabled( false );

    m_fileExt << ".cr2"
              << ".cr3"
              << ".crf"
              << ".dng"
              << ".nef"
              << ".raw"
              << ".sr2"
              << ".srf"
              << ".nrw"
              << ".arw";

    m_filter = "RAW images (";
    foreach( QString fext, m_fileExt )
    {
        m_filter.append( QString( "*%1 " ).arg( fext ) );
    }
    m_filter.chop( 1 );
    m_filter.append( ")" );
}

//Destructor
TranscodeDialog::~TranscodeDialog()
{
    QSettings set( QSettings::UserScope, "magiclantern.MLVApp", "MLVApp" );
    set.setValue( "lastTranscodeSourcePath", m_lastSourcePath );
    set.setValue( "lastTranscodeTargetPath", m_lastTargetPath );
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
    QStringList files = QFileDialog::getOpenFileNames( this, tr("Open one or more RAW files..."),
                                                    m_lastSourcePath,
                                                    m_filter );

    if( files.empty() ) return;
    QString file = files.at( 0 );
    m_lastSourcePath = file.left( file.lastIndexOf( "/" ) );

    foreach( QString file, files )
    {
        QString fileName = QFileInfo( file ).fileName();

        QTreeWidgetItem *pItem = new QTreeWidgetItem( ui->treeWidget );
        pItem->setText( 0, fileName );
        pItem->setText( 1, file );
        fileName = fileName.left( fileName.lastIndexOf(".") ).append( ".MLV" );
        pItem->setText( 2, fileName );
    }

    ui->pushButtonTranscode->setEnabled( true );
}

//Add folders with frames for single MLVs
void TranscodeDialog::on_pushButtonAddSequence_clicked()
{
    QString folder = QFileDialog::getExistingDirectory(this, tr("Open RAW Sequences"),
                                                            m_lastSourcePath,
                                                            QFileDialog::ShowDirsOnly
                                                            | QFileDialog::DontResolveSymlinks);

    if( !folder.count() ) return;
    m_lastSourcePath = folder.left( folder.lastIndexOf( "/" ) );

    QString path = folder.right( folder.count() - folder.lastIndexOf( "/" ) - 1 );

    QTreeWidgetItem *pItem = new QTreeWidgetItem( ui->treeWidget );
    pItem->setText( 0, path );
    //pItem->setText( 1, file );
    path.append( ".MLV" );
    pItem->setText( 2, path );

    QStringList list = QDir( folder ).entryList( QStringList() << "*.*", QDir::Files );
    int num = 0;
    foreach( QString file, list )
    {
        if( !supportedFileType( file ) ) continue;
        QTreeWidgetItem *pChild = new QTreeWidgetItem( pItem );
        QString fileName = QFileInfo( file ).fileName();
        pChild->setText( 0, fileName );
        pChild->setText( 1, QString( "%1/%2" ).arg( folder ).arg( file ) );
        num++;
    }
    if( !num )
    {
        ui->treeWidget->takeTopLevelItem( ui->treeWidget->topLevelItemCount() - 1 );
    }
    else
    {
        ui->pushButtonTranscode->setEnabled( true );
    }
}

//Check if fileName is a supported file type
bool TranscodeDialog::supportedFileType(QString fileName)
{
    foreach( QString fext, m_fileExt )
    {
        if( fileName.endsWith( fext, Qt::CaseInsensitive ) ) return true;
    }
    return false;
}

//Transcode via RAW2MLV
void TranscodeDialog::on_pushButtonTranscode_clicked()
{

    QString folderName = QFileDialog::getExistingDirectory(this, tr("Choose Destination Folder"),
                                                      m_lastTargetPath,
                                                      QFileDialog::ShowDirsOnly
                                                      | QFileDialog::DontResolveSymlinks);
    if( folderName.length() == 0 ) return;
    m_lastTargetPath = folderName;
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

        //Compress
        if( ui->checkBoxCompress->isChecked() )
        {
            command.append( QString( " --compression 1" ) );
        }

        //Bitdepth
        switch( ui->comboBoxBitDepth->currentIndex() )
        {
        case 1: command.append( QString( " -b 10" ) );
            break;
        case 2: command.append( QString( " -b 12" ) );
            break;
        case 3: command.append( QString( " -b 14" ) );
            break;
        case 4: command.append( QString( " -b 16" ) );
            break;
        default:
            break;
        }

        //Framerate
        switch( ui->comboBoxFps->currentIndex() )
        {
        case 0: command.append( QString( " -f 24000 1001" ) );
            break;
        case 1: command.append( QString( " -f 24 1" ) );
            break;
        case 2: command.append( QString( " -f 25 1" ) );
            break;
        case 3: command.append( QString( " -f 30000 1001" ) );
            break;
        case 4: command.append( QString( " -f 30 1" ) );
            break;
        case 5: command.append( QString( " -f 48 1" ) );
            break;
        case 6: command.append( QString( " -f 50 1" ) );
            break;
        case 7: command.append( QString( " -f 60000 1001" ) );
            break;
        case 8: command.append( QString( " -f 60 1" ) );
            break;
        default:
            break;
        }

        qDebug() << command;
        QProcess process;
        process.execute( command );

        m_importList.append( QString( "%1/%2" ).arg( folderName ).arg( ui->treeWidget->topLevelItem(i)->text(2) ) );
    }
    close();
}
