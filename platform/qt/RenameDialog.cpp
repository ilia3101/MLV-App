/*!
 * \file RenameDialog.cpp
 * \author masc4ii
 * \copyright 2022
 * \brief file rename window
 */

#include "RenameDialog.h"
#include "ui_RenameDialog.h"
#include <QtGlobal>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QRegExpValidator>
#else
#include <QRegularExpressionValidator>
#endif

RenameDialog::RenameDialog(QWidget *parent, QString clipName) :
    QDialog(parent),
    ui(new Ui::RenameDialog)
{
    ui->setupUi(this);
    ui->lineEdit->setText( clipName );
}

RenameDialog::~RenameDialog()
{
    delete ui;
}

QString RenameDialog::clipName()
{
    return ui->lineEdit->text();
}

void RenameDialog::on_lineEdit_textChanged(const QString &arg1)
{
#if QT_VERSION <= QT_VERSION_CHECK(5, 0, 0)
    QRegExp rx("*.MLV");
    rx.setPatternSyntax(QRegExp::Wildcard);
    ui->pushButtonRename->setEnabled( rx.exactMatch( arg1 ) );
#else
    QRegularExpression rx("*.MLV");
    ui->pushButtonRename->setEnabled( rx.match( arg1 ).hasMatch() );
#endif
}

