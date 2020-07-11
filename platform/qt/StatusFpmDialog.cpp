#include "StatusFpmDialog.h"
#include "ui_StatusFpmDialog.h"

StatusFpmDialog::StatusFpmDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StatusFpmDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint );
}

StatusFpmDialog::~StatusFpmDialog()
{
    delete ui;
}
