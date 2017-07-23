#include "StatusDialog.h"

StatusDialog::StatusDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StatusDialog)
{
    ui->setupUi(this);
}

StatusDialog::~StatusDialog()
{
    delete ui;
}
