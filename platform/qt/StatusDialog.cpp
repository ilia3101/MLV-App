/*!
 * \file StatusDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a progress bar
 */

#include "StatusDialog.h"

//Constructor
StatusDialog::StatusDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StatusDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint );
}

//Destructor
StatusDialog::~StatusDialog()
{
    delete ui;
}

//Abort clicked
void StatusDialog::on_pushButtonAbort_clicked()
{
    emit abortPressed();
}
