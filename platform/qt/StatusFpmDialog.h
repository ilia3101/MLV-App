#ifndef STATUSFPMDIALOG_H
#define STATUSFPMDIALOG_H

#include <QDialog>

namespace Ui {
class StatusFpmDialog;
}

class StatusFpmDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StatusFpmDialog(QWidget *parent = 0);
    ~StatusFpmDialog();

private:
    Ui::StatusFpmDialog *ui;
};

#endif // STATUSFPMDIALOG_H
