/*!
 * \file FcpxmlAssistantDialog.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Import MLV files to session, which were used in FCPXML project
 */

#include "QFileDialog"
#include "QFile"
#include "QXmlStreamReader"

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
        if( Rxml.isStartElement() && Rxml.name() == "clip" )
        {
            //Read clipname
            if( Rxml.attributes().count() != 0 )
            {
                QString clip = Rxml.attributes().at(0).value().toString();

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
                    QTableWidgetItem *item = new QTableWidgetItem( clip );
                    ui->tableWidget->setItem( row, 0, item );
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
    if( ui->tableWidget->rowCount() == 0 ) return;
    if( ui->lineEditFcpxml->text().count() == 0 ) return;
    if( ui->lineEditMlvFolder->text().count() == 0 ) return;

    m_fileList.clear();

    for( int i = 0; i < ui->tableWidget->rowCount(); i++ )
    {
        QString mlvFileName = ui->lineEditMlvFolder->text();
        if( !mlvFileName.endsWith( '/' ) || !mlvFileName.endsWith( '\\' ) )
        {
            mlvFileName.append( '/' );
        }
        mlvFileName.append( ui->tableWidget->item( i, 0 )->text() ).append( ".MLV" );

        if( QFile( mlvFileName ).exists() )
        {
            QTableWidgetItem *item = new QTableWidgetItem( mlvFileName );
            ui->tableWidget->setItem( i, 1, item );
            m_fileList.append( mlvFileName );
        }
    }


    //Status
    ui->labelStatus->setText( QString( "Found %1 MLV file(s)" ).arg( m_fileList.count() ) );
}

//Select MLV folder
void FcpxmlAssistantDialog::on_pushButtonMlv_clicked()
{
    //Folder Dialog
    QString path = QDir::homePath();
    QString folderName = QFileDialog::getExistingDirectory(this, tr("Chose MLV Folder"),
                                                      path,
                                                      QFileDialog::ShowDirsOnly
                                                      | QFileDialog::DontResolveSymlinks);

    if( folderName.length() == 0 ) return;
    //Show folder name
    ui->lineEditMlvFolder->setText( folderName );

    searchMlvs();
}
