/*!
 * \file InfoDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a table for information
 */

#include "InfoDialog.h"

InfoDialog::InfoDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::InfoDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Tool | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint );
}

InfoDialog::~InfoDialog()
{
    delete ui;
}

void InfoDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    adjustSize();
}
