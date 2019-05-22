#include "UserManualDialog.h"
#include "ui_UserManualDialog.h"
#include <QFile>
#include <QTextStream>

UserManualDialog::UserManualDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UserManualDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint );
    QFile file(":/help/help/help.htm");
    file.open(QFile::ReadOnly | QFile::Text);
    QTextStream stream(&file);
    ui->textBrowser->setHtml(stream.readAll());
}

UserManualDialog::~UserManualDialog()
{
    delete ui;
}
