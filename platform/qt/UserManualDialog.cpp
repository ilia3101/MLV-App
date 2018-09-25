#include "UserManualDialog.h"
#include "ui_UserManualDialog.h"

UserManualDialog::UserManualDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UserManualDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
}

UserManualDialog::~UserManualDialog()
{
    delete ui;
}
