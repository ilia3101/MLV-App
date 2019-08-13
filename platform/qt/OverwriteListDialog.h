/*!
 * \file OverwriteListDialog.h
 * \author masc4ii
 * \copyright 2019
 * \brief Show files to be overwritten, or not
 */

#ifndef OVERWRITELISTDIALOG_H
#define OVERWRITELISTDIALOG_H

#include <QDialog>
#include "ui_OverwriteListDialog.h"

namespace Ui {
class OverwriteListDialog;
}

class OverwriteListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OverwriteListDialog(QWidget *parent = 0);
    Ui::OverwriteListDialog *ui;
    ~OverwriteListDialog();

};

#endif // OVERWRITELISTDIALOG_H
