/*!
 * \file SingleFrameExportDialog.h
 * \author masc4ii, bouncyball
 * \copyright 2018
 * \brief A class for exporting single frames
 */

#ifndef SINGLEFRAMEEXPORTDIALOG_H
#define SINGLEFRAMEEXPORTDIALOG_H

#include <QDialog>
#include "../../src/mlv_include.h"

namespace Ui {
class SingleFrameExportDialog;
}

class SingleFrameExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SingleFrameExportDialog(QWidget *parent = 0, mlvObject_t *pMlvObject = 0, QString fileName = QString( "" ), int frameNr = 0, double stretchX = 1.0, double stretchY = 1.0);
    ~SingleFrameExportDialog();

private slots:
    void on_pushButtonCancel_clicked();
    void on_pushButtonOk_clicked();

private:
    void exportViaQt( void );
    void exportDng( void );

    Ui::SingleFrameExportDialog *ui;
    mlvObject_t *m_pMlvObject;
    QString m_fileName;
    int m_frameNr;
    double m_stretchX;
    double m_stretchY;
};

#endif // SINGLEFRAMEEXPORTDIALOG_H
