/*!
 * \file InfoDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a table for information
 */

#ifndef INFODIALOG_H
#define INFODIALOG_H

#include <QDialog>
#include "ui_InfoDialog.h"

namespace Ui {
class InfoDialog;
}

class InfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InfoDialog(QWidget *parent = 0);
    ~InfoDialog();
    Ui::InfoDialog *ui;

};

#endif // INFODIALOG_H
