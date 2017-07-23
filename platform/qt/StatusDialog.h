#ifndef STATUSDIALOG_H
#define STATUSDIALOG_H

#include <QDialog>
#include "ui_StatusDialog.h"

namespace Ui {
class StatusDialog;
}

class StatusDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StatusDialog(QWidget *parent = 0);
    ~StatusDialog();
    Ui::StatusDialog *ui;
};

#endif // STATUSDIALOG_H
