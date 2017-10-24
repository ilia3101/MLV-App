/*!
 * \file TestDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A dialog to test new elements
 */

#include "TestDialog.h"
#include "ui_TestDialog.h"

TestDialog::TestDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TestDialog)
{
    ui->setupUi(this);
}

TestDialog::~TestDialog()
{
    delete ui;
}

void TestDialog::on_groupBox_toggled(bool arg1)
{
    ui->widget1->setVisible( arg1 );
}

void TestDialog::on_groupBox_2_toggled(bool arg1)
{
    ui->widget2->setVisible( arg1 );
}
