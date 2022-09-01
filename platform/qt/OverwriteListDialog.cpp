/*!
 * \file OverwriteListDialog.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Show files to be overwritten, or not
 */

#include "OverwriteListDialog.h"

OverwriteListDialog::OverwriteListDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OverwriteListDialog)
{
    ui->setupUi(this);
}

OverwriteListDialog::~OverwriteListDialog()
{
    delete ui;
}

void OverwriteListDialog::on_pushButtonAbort_clicked()
{
    this->done( 0 );
}

void OverwriteListDialog::on_pushButtonOverwrite_clicked()
{
    this->done( 1 );
}

void OverwriteListDialog::on_pushButtonSkip_clicked()
{
    this->done( 2 );
}

