/*!
 * \file FcpxmlSelectDialog.h
 * \author masc4ii
 * \copyright 2018
 * \brief Assistant, which helps selection clips in session in dependency to clips which were used in FCPXML project
 */

#ifndef FcpxmlSelectDialog_H
#define FcpxmlSelectDialog_H

#include <QDialog>
#include <QItemSelectionModel>
#include <QSortFilterProxyModel>
#include "SessionModel.h"

namespace Ui {
class FcpxmlSelectDialog;
}

class FcpxmlSelectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FcpxmlSelectDialog(QWidget *parent, SessionModel *pModel, QSortFilterProxyModel* pProxyModel, QItemSelectionModel* pSelectionModel);
    ~FcpxmlSelectDialog();

private slots:
    void on_pushButtonFcpxml_clicked();
    void on_checkBoxInvert_clicked();
    void on_pushButtonSelect_clicked();

private:
    Ui::FcpxmlSelectDialog *ui;
    SessionModel *m_pModel;
    QItemSelectionModel* m_pSelectionModel;
    QSortFilterProxyModel* m_pProxyModel;
    void xmlParser( QString fileName );
    void counter();
};

#endif // FcpxmlSelectDialog_H
