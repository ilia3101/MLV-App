/*!
 * \file StatusDialog.h
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a progress bar
 */

#ifndef STATUSDIALOG_H
#define STATUSDIALOG_H

#include <QDialog>
#include <QDateTime>
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
    void setTotalFrames( uint32_t frames );
    void drawTimeFromToDoFrames( uint32_t frames );
    void startExportTime( void );

private:
    uint32_t m_totalTodoFrames;
    QDateTime m_startTime;

signals:
    void abortPressed( void );

public slots:
    void incrementProgressBar( void )
    {
        ui->progressBar->setValue( ui->progressBar->value() + 1 );
        ui->progressBar->repaint();
        qApp->processEvents();
    }
private slots:
    void on_pushButtonAbort_clicked();
};

#endif // STATUSDIALOG_H
