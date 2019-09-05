/*!
 * \file PixelMapListDialog.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Show all installed focus pixel maps
 */

#include "PixelMapListDialog.h"
#include "ui_PixelMapListDialog.h"
#include <QDir>
#include <QTreeWidgetItem>

PixelMapListDialog::PixelMapListDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PixelMapListDialog)
{
    ui->setupUi(this);

    QDir directory( QCoreApplication::applicationDirPath() );
    QStringList fpms = directory.entryList( QStringList() << "*.fpm" << "*.FPM", QDir::Files );

    QTreeWidgetItem *cams = new QTreeWidgetItem(ui->treeWidget);
    cams->setText( 0, tr( "Canon EOS 650D / Rebel T4i / Kiss X6i" ) );
    foreach( QString fpm, fpms )
    {
        if( fpm.contains( "80000301" ) )
        {
            QTreeWidgetItem *fpmItem = new QTreeWidgetItem(cams);
            fpmItem->setText(0, fpm);
            fpms.removeAll( fpm );
        }
    }
    cams = new QTreeWidgetItem(ui->treeWidget);
    cams->setText( 0, tr( "Canon EOS 700D / Rebel T5i / Kiss X7i" ) );
    foreach( QString fpm, fpms )
    {
        if( fpm.contains( "80000326" ) )
        {
            QTreeWidgetItem *fpmItem = new QTreeWidgetItem(cams);
            fpmItem->setText(0, fpm);
            fpms.removeAll( fpm );
        }
    }
    cams = new QTreeWidgetItem(ui->treeWidget);
    cams->setText( 0, tr( "Canon EOS M" ) );
    foreach( QString fpm, fpms )
    {
        if( fpm.contains( "80000331" ) )
        {
            QTreeWidgetItem *fpmItem = new QTreeWidgetItem(cams);
            fpmItem->setText(0, fpm);
            fpms.removeAll( fpm );
        }
    }
    cams = new QTreeWidgetItem(ui->treeWidget);
    cams->setText( 0, tr( "Canon 100D / Rebel SL1 / Kiss X7" ) );
    foreach( QString fpm, fpms )
    {
        if( fpm.contains( "80000346" ) )
        {
            QTreeWidgetItem *fpmItem = new QTreeWidgetItem(cams);
            fpmItem->setText(0, fpm);
            fpms.removeAll( fpm );
        }
    }
    cams = new QTreeWidgetItem(ui->treeWidget);
    cams->setText( 0, tr( "Canon EOS M2" ) );
    foreach( QString fpm, fpms )
    {
        if( fpm.contains( "80000355" ) )
        {
            QTreeWidgetItem *fpmItem = new QTreeWidgetItem(cams);
            fpmItem->setText(0, fpm);
            fpms.removeAll( fpm );
        }
    }
    cams = new QTreeWidgetItem(ui->treeWidget);
    cams->setText( 0, tr( "Others" ) );
    foreach( QString fpm, fpms )
    {
        QTreeWidgetItem *fpmItem = new QTreeWidgetItem(cams);
        fpmItem->setText(0, fpm);
        fpms.removeAll( fpm );
    }
}

PixelMapListDialog::~PixelMapListDialog()
{
    delete ui;
}
