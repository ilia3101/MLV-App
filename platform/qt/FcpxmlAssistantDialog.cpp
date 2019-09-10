/*!
 * \file FcpxmlAssistantDialog.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Import MLV files to session, which were used in FCPXML project
 */

#include "QFileDialog"
#include "QFile"
#include "QXmlStreamReader"
#include "QDirIterator"

#include "FcpxmlAssistantDialog.h"
#include "ui_FcpxmlAssistantDialog.h"

//Constructor
FcpxmlAssistantDialog::FcpxmlAssistantDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FcpxmlAssistantDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint );
    m_fileList.clear();
    ui->labelStatus->setText( "" );
    ui->pushButtonImport->setEnabled( false );
}

//Destructor
FcpxmlAssistantDialog::~FcpxmlAssistantDialog()
{
    delete ui;
}

//Return the file names of found MLV's
QStringList FcpxmlAssistantDialog::getFileNames()
{
    return m_fileList;
}

//Open and parse xml
void FcpxmlAssistantDialog::on_pushButtonFcpxml_clicked()
{
    QString path = QDir::homePath();
    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open FCPXML..."), path,
                                           tr("FCPXML files (*.fcpxml)"));

    //Abort selected
    if( fileName.count() == 0 ) return;
    //Show filename
    ui->lineEditFcpxml->setText( fileName );
    xmlParser( fileName );
    searchMlvs();
}

//Search in the fcpxml for <clip... entries and bring to table
void FcpxmlAssistantDialog::xmlParser(QString fileName)
{
    ui->labelStatus->setText( "" );
    ui->pushButtonImport->setEnabled( false );
    while( ui->tableWidget->rowCount() > 0 ) ui->tableWidget->removeRow( 0 );

    //Open a XML stream for the file
    QXmlStreamReader Rxml;
    QFile file(fileName);
    if( !file.open(QIODevice::ReadOnly | QFile::Text) )
    {
        return;
    }

    //Parse
    Rxml.setDevice(&file);
    while( !Rxml.atEnd() )
    {
        Rxml.readNext();
        if( Rxml.isStartElement() && ( Rxml.name() == "clip" || Rxml.name() == "asset-clip" || Rxml.name() == "asset" ) )
        {
            //Read clipname
            if( Rxml.attributes().count() != 0 )
            {
                QString clip;
                for( int i = 0; i < Rxml.attributes().count(); i++ )
                {
                    if( Rxml.attributes().at(i).name() == QString( "name" ) )
                    {
                        clip = Rxml.attributes().at(i).value().toString();
                    }
                }
                if( clip.isEmpty() ) continue;

                //Clean the davinci resolve naming to a correct real name
                if( clip.contains( ".[" ) ) clip = clip.left( clip.indexOf( ".[" ) );
                if( clip.right(5).contains( "." ) ) clip = clip.left( clip.lastIndexOf( "." ) );

                int row = ui->tableWidget->rowCount();
                bool add = true;
                for( int i = 0; i < row; i++ )
                {
                    if( ui->tableWidget->item( i, 0 )->text() == clip )
                    {
                        add = false;
                        break;
                    }
                }

                if( add )
                {
                    ui->tableWidget->insertRow( row );
                    QTableWidgetItem *item1 = new QTableWidgetItem( clip );
                    ui->tableWidget->setItem( row, 0, item1 );
                    QTableWidgetItem *item2 = new QTableWidgetItem( "" );
                    ui->tableWidget->setItem( row, 1, item2 );
                }
            }
        }
    }
    file.close();
}

//Search filenames from table in given folder
void FcpxmlAssistantDialog::searchMlvs()
{
    ui->labelStatus->setText( "" );
    ui->pushButtonImport->setEnabled( false );
    if( ui->tableWidget->rowCount() == 0 ) return;
    if( ui->lineEditFcpxml->text().count() == 0 ) return;
    if( ui->lineEditMlvFolder->text().count() == 0 ) return;

    m_fileList.clear();

    for( int i = 0; i < ui->tableWidget->rowCount(); i++ )
    {
        QString name = QString( ui->tableWidget->item( i, 0 )->text() ).append( ".MLV" );
        QString dir = ui->lineEditMlvFolder->text();
        if( !dir.endsWith( '/' ) || !dir.endsWith( '\\' ) )
        {
            dir.append( '/' );
        }

        //Prepare scan option
        QDirIterator::IteratorFlag recOrNot = QDirIterator::Subdirectories;
        if( !ui->checkBoxScanRecursively->isChecked() ) recOrNot = QDirIterator::NoIteratorFlags;

        //Scan
        QDirIterator it(dir, QStringList() << name, QDir::Files, recOrNot);

        //Show results
        if( it.hasNext() )
        {
            QString entry = it.next();
            ui->tableWidget->item( i, 1 )->setText( entry );
            m_fileList.append( entry );
        }
        else
        {
            ui->tableWidget->item( i, 1 )->setText( "" );
        }
    }


    //Status
    ui->labelStatus->setText( tr( "Found %1 MLV file(s)" ).arg( m_fileList.count() ) );
    if( m_fileList.count() > 0 ) ui->pushButtonImport->setEnabled( true );
    else ui->pushButtonImport->setEnabled( false );
}

//Select MLV folder
void FcpxmlAssistantDialog::on_pushButtonMlv_clicked()
{
    //Folder Dialog
    QString path = QDir::homePath();
    QString folderName = QFileDialog::getExistingDirectory(this, tr("Choose MLV Folder"),
                                                      path,
                                                      QFileDialog::ShowDirsOnly
                                                      | QFileDialog::DontResolveSymlinks);

    if( folderName.length() == 0 ) return;
    //Show folder name
    ui->lineEditMlvFolder->setText( folderName );

    searchMlvs();
}

//Changed recursively checkbox -> rescan
void FcpxmlAssistantDialog::on_checkBoxScanRecursively_clicked()
{
    searchMlvs();
}
