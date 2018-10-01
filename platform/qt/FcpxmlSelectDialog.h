/*!
 * \file FcpxmlSelectDialog.h
 * \author masc4ii
 * \copyright 2018
 * \brief Assistant, which helps selection clips in session in dependency to clips which were used in FCPXML project
 */

#ifndef FcpxmlSelectDialog_H
#define FcpxmlSelectDialog_H

#include <QDialog>
#include <QListWidget>

namespace Ui {
class FcpxmlSelectDialog;
}

class FcpxmlSelectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FcpxmlSelectDialog(QWidget *parent = 0, QListWidget *list = 0);
    ~FcpxmlSelectDialog();

private slots:
    void on_pushButtonFcpxml_clicked();
    void on_checkBoxInvert_clicked();
    void on_pushButtonSelect_clicked();

private:
    Ui::FcpxmlSelectDialog *ui;
    QListWidget *m_list;
    void xmlParser( QString fileName );
    void counter();
};

#endif // FcpxmlSelectDialog_H
