/*!
 * \file TestDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief A dialog to test new elements
 */

#ifndef TESTDIALOG_H
#define TESTDIALOG_H

#include <QDialog>

namespace Ui {
class TestDialog;
}

class TestDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TestDialog(QWidget *parent = 0);
    ~TestDialog();

private slots:
    void on_groupBox_toggled(bool arg1);
    void on_groupBox_2_toggled(bool arg1);

private:
    Ui::TestDialog *ui;
};

#endif // TESTDIALOG_H
