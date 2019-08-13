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
