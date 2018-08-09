/*!
 * \file FcpxmlAssistantDialog.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Import MLV files to session, which were used in FCPXML project
 */

#include "QFileDialog"
#include "QFile"
#include "QDebug"
#include "QXmlStreamReader"

#include "FcpxmlAssistantDialog.h"
#include "ui_FcpxmlAssistantDialog.h"

FcpxmlAssistantDialog::FcpxmlAssistantDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FcpxmlAssistantDialog)
{
    ui->setupUi(this);
}

FcpxmlAssistantDialog::~FcpxmlAssistantDialog()
{
    delete ui;
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
}

//Search in the fcpxml for <clip... entries and bring to table
void FcpxmlAssistantDialog::xmlParser(QString fileName)
{
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
