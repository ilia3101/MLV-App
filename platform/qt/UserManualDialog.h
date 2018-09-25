#ifndef USERMANUALDIALOG_H
#define USERMANUALDIALOG_H

#include <QDialog>

namespace Ui {
class UserManualDialog;
}

class UserManualDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserManualDialog(QWidget *parent = nullptr);
    ~UserManualDialog();

private:
    Ui::UserManualDialog *ui;
};

#endif // USERMANUALDIALOG_H
