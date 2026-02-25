/*!
 * \file StatusDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a progress bar
 */

#include "StatusDialog.h"
#include <QDebug>
#include <QKeyEvent>

//Constructor
StatusDialog::StatusDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StatusDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint );
}

//Destructor
StatusDialog::~StatusDialog()
{
    delete ui;
}

//Set total number of frames, for remaining time calc
void StatusDialog::setTotalFrames(uint32_t frames)
{
    m_totalTodoFrames = frames;
    ui->labelEstimatedTime->setText( "" );
}

//Draw remaining time to UI, input is todoFrames
void StatusDialog::drawTimeFromToDoFrames(uint32_t framesToDo)
{
    QDateTime currentTime = QDateTime::currentDateTime();
    quint64 secsGone = m_startTime.secsTo(currentTime);
    uint32_t framesDone = m_totalTodoFrames - framesToDo;
    
    // We don't like to divide by 0
    if( framesDone == 0 ) return;

    // Compute current seconds per frame
    double secsPerFrame = (double)secsGone / (double)framesDone;

    if (m_avgSecsPerFrame <= 0.0)
    {
        m_avgSecsPerFrame = secsPerFrame;
    }
    else
    {
        // Adaptive smoothing (better accuracy with less jitter)
        double progress = (double)framesDone / (double)m_totalTodoFrames;
        double alpha;

        if (progress < 0.0625)
            // very responsive early    
            alpha = 0.5;
        
        else if (progress < 0.125)
            // stable later    
            alpha = 0.25;
        
        else
            alpha = 0.125;

        m_avgSecsPerFrame = (1.0 - alpha) * m_avgSecsPerFrame + alpha * secsPerFrame;
    }

    // Compute remaining time
    double secsRemainingDouble = m_avgSecsPerFrame * framesToDo;

    if (secsRemainingDouble < 0.0) secsRemainingDouble = 0.0;

    quint64 duration = (quint64)(secsRemainingDouble + 0.5);

    int seconds = (int) (duration % 60);
    duration /= 60;
    int minutes = (int) (duration % 60);
    duration /= 60;
    int hours = (int) (duration);

    ui->labelEstimatedTime->setText( QString( "ETA: %1h%2m%3s" )
            .arg( hours, 2, 10, QChar('0') )
            .arg( minutes, 2, 10, QChar('0') )
            .arg( seconds, 2, 10, QChar('0') ) );
}

//Start Time for remaining time calculation
void StatusDialog::startExportTime()
{
    m_startTime = QDateTime::currentDateTime();
    m_avgSecsPerFrame = 0.0;
    ui->labelEstimatedTime->setText( "" );
}

void StatusDialog::keyPressEvent( QKeyEvent *event )
{
    if( event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Return )
    {
        // Ignoriere diese Tasten vollständig
        event->ignore();
        return;
    }

    // Alle anderen Tasten normal behandeln
    QDialog::keyPressEvent( event );
}

//Abort clicked
void StatusDialog::on_pushButtonAbort_clicked()
{
    emit abortPressed();
}
