/*!
 * \file ReceiptCopyMaskDialog.h
 * \author masc4ii
 * \copyright 2018
 * \brief A dialog to setup a receipt mask
 */

#ifndef RECEIPTCOPYMASKDIALOG_H
#define RECEIPTCOPYMASKDIALOG_H

#include <QDialog>
#include "ui_ReceiptCopyMaskDialog.h"

namespace Ui {
class ReceiptCopyMaskDialog;
}

class ReceiptCopyMaskDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReceiptCopyMaskDialog(QWidget *parent = 0);
    ~ReceiptCopyMaskDialog();
    Ui::ReceiptCopyMaskDialog *ui;

private slots:
    void checkBoxRawCorrectionState( void );
    void checkBoxProcessingState( void );
    void checkBoxDetailsState( void );
    void on_checkBoxRawCorrection_clicked(bool checked);
    void on_checkBoxProcessing_clicked(bool checked);
    void on_checkBoxDetails_clicked(bool checked);
    void on_pushButtonAll_clicked();
    void on_pushButtonNone_clicked();
};

#endif // RECEIPTCOPYMASKDIALOG_H
