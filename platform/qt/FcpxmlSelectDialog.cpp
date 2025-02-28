/*!
 * \file FcpxmlSelectDialog.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Assistant, which helps selection clips in session in dependency to clips which were used in FCPXML project
 */

#include "QFileDialog"
#include "QFile"
#include "QXmlStreamReader"
#include "QDirIterator"

#include "FcpxmlSelectDialog.h"
#include "ui_FcpxmlSelectDialog.h"

//Constructor
FcpxmlSelectDialog::FcpxmlSelectDialog(QWidget *parent, SessionModel* pModel, QSortFilterProxyModel *pProxyModel, QItemSelectionModel *pSelectionModel) :
    QDialog(parent),
    ui(new Ui::FcpxmlSelectDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint );
    m_pModel = pModel;
    m_pProxyModel = pProxyModel;
    m_pSelectionModel = pSelectionModel;
    ui->checkBoxInvert->setChecked( false );

    for( int i = 0; i < m_pModel->rowCount( QModelIndex() ); i++ )
    {
        ui->tableWidget->insertRow( i );
        QTableWidgetItem *item1 = new QTableWidgetItem( m_pModel->clip( i )->getName() );
        item1->setCheckState( Qt::Unchecked );
        item1->setFlags( item1->flags() ^ Qt::ItemIsUserCheckable );
        ui->tableWidget->setItem( i, 0, item1 );
    }

    counter();
}

//Destructor
FcpxmlSelectDialog::~FcpxmlSelectDialog()
{
    delete ui;
}

//Open and parse xml
void FcpxmlSelectDialog::on_pushButtonFcpxml_clicked()
{
    QString path = QDir::homePath();
    QString fileName = QFileDialog::getOpenFileName(this,
                                           tr("Open FCPXML..."), path,
                                           tr("FCPXML files (*.fcpxml *.xml)"));

    //Abort selected
    if( fileName.count() == 0 ) return;
    //Show filename
    ui->lineEditFcpxml->setText( fileName );
    xmlParser( fileName );
    counter();
}

//Search in the fcpxml for <clip... entries and bring to table
void FcpxmlSelectDialog::xmlParser(QString fileName)
{
    ui->labelStatus->setText( "" );

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
        if( Rxml.isStartElement() && ( Rxml.name() == QString( "clip" ) || Rxml.name() == QString( "asset-clip" ) || Rxml.name() == QString( "asset" ) )  )
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
                for( int i = 0; i < row; i++ )
                {
                    if( ui->tableWidget->item( i, 0 )->text().contains( clip + ".mlv", Qt::CaseInsensitive) )
                    {
                        if( !ui->checkBoxInvert->isChecked() ) ui->tableWidget->item( i, 0 )->setCheckState( Qt::Checked );
                        else ui->tableWidget->item( i, 0 )->setCheckState( Qt::Unchecked );
                        break;
                    }
                }
            }
        }
        if( Rxml.isStartElement() && ( Rxml.name() == QString( "clipitem" ) ) )
        {
            while( !(Rxml.isEndElement() && ( Rxml.name() == QString( "clipitem" ) ) ) )
            {
                Rxml.readNext();
                if( Rxml.isStartElement() && ( Rxml.name() == QString( "name" ) ) )
                {
                    QString clip = Rxml.readElementText();
                    if( clip.isEmpty() ) continue;

                    //Clean the davinci resolve naming to a correct real name
                    if( clip.contains( ".[" ) ) clip = clip.left( clip.indexOf( ".[" ) );
                    if( clip.right(5).contains( "." ) ) clip = clip.left( clip.lastIndexOf( "." ) );

                    int row = ui->tableWidget->rowCount();
                    for( int i = 0; i < row; i++ )
                    {
                        if( ui->tableWidget->item( i, 0 )->text().contains( clip + ".mlv", Qt::CaseInsensitive) )
                        {
                            if( !ui->checkBoxInvert->isChecked() ) ui->tableWidget->item( i, 0 )->setCheckState( Qt::Checked );
                            else ui->tableWidget->item( i, 0 )->setCheckState( Qt::Unchecked );
                            break;
                        }
                    }

                    while( !(Rxml.isEndElement() && ( Rxml.name() == QString( "clipitem" ) ) ) )
                    {
                        Rxml.readNext();
                    }
                }
            }
        }
    }
    file.close();
}

//Set status label and select button
void FcpxmlSelectDialog::counter()
{
    int cnt = 0;
    for( int i = 0; i < ui->tableWidget->rowCount(); i++ )
    {
        if( ui->tableWidget->item( i, 0 )->checkState() == Qt::Checked ) cnt++;
    }

    ui->labelStatus->setText( tr( "%1 clip(s) selected" ).arg( cnt ) );

    if( cnt == 0 ) ui->pushButtonSelect->setEnabled( false );
    else ui->pushButtonSelect->setEnabled( true );
}

//Invert all checkboxes in table
void FcpxmlSelectDialog::on_checkBoxInvert_clicked()
{
    for( int i = 0; i < ui->tableWidget->rowCount(); i++ )
    {
        if( ui->tableWidget->item( i, 0 )->checkState() == Qt::Unchecked ) ui->tableWidget->item( i, 0 )->setCheckState( Qt::Checked );
        else ui->tableWidget->item( i, 0 )->setCheckState( Qt::Unchecked );
    }

    counter();
}

//Select and quit
void FcpxmlSelectDialog::on_pushButtonSelect_clicked()
{
    m_pSelectionModel->clearSelection();
    for( int i = 0; i < m_pModel->rowCount( QModelIndex() ); i++ )
    {
        if( ui->tableWidget->item( i, 0 )->checkState() == Qt::Checked ) m_pSelectionModel->select( m_pProxyModel->mapFromSource( m_pModel->index( i, 0, QModelIndex() ) ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
    }
    accept();
}
