/*!
 * \file RenameDialog.h
 * \author masc4ii
 * \copyright 2022
 * \brief file rename window
 */

#ifndef RENAMEDIALOG_H
#define RENAMEDIALOG_H

#include <QDialog>

namespace Ui {
class RenameDialog;
}

class RenameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RenameDialog(QWidget *parent = nullptr, QString clipName = "myclip.MLV");
    ~RenameDialog();
    QString clipName( void );

private slots:
    void on_lineEdit_textChanged(const QString &arg1);

private:
    Ui::RenameDialog *ui;
};

#endif // RENAMEDIALOG_H
