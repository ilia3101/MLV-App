/*!
 * \file FcpxmlAssistantDialog.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Import MLV files to session, which were used in FCPXML project
 */

#include "QFileDialog"

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

    ui->lineEditFcpxml->setText( fileName );
}
