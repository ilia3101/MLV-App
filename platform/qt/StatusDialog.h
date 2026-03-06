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
    void exportStart( int numberOfJobs, uint32_t totalFrames );
    void setJobFrames( uint32_t jobFrames );
    void drawTimeFromToDoFrames( uint32_t framesToDo );
    void totalProgressBar( uint32_t framesToDo );
    bool isPaused( void );
    void togglePauseResume( int state );
    bool m_isLoopRunning;

protected:
    void keyPressEvent( QKeyEvent *event ) override;

private:
    uint32_t m_totalTodoFrames;
    uint32_t m_jobFrames;
    int32_t m_jobFramesDone;
    QDateTime m_startTime;
    QDateTime m_jobStartTime;
    QDateTime m_pausedTime;
    bool m_paused;
    QString getTimeString( double secsRemaining );

signals:
    void resumePressed( void );
    void abortPressed( void );

private slots:
    void on_pushButtonPause_clicked();
    void on_pushButtonAbort_clicked();
};

#endif // STATUSDIALOG_H
