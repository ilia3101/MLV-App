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
    // We don't like to divide by 0
    if( !m_totalTodoFrames || framesToDo >= m_totalTodoFrames ) return;

    uint32_t framesDone = m_totalTodoFrames - framesToDo;

    QDateTime currentTime = QDateTime::currentDateTime();
    double secsGone = std::max(0.0, m_startTime.msecsTo(currentTime) / 1000.0);

    // Compute current seconds per frame
    double secsPerFrame = secsGone / framesDone;

    if( m_avgSecsPerFrame <= 0.0 )
    {
        m_avgSecsPerFrame = secsPerFrame;
    }
    else
    {
        // Adaptive smoothing (better accuracy with less jitter)
        double progress = static_cast<double>(framesDone) / m_totalTodoFrames;
        double alpha = 0.5;

        // Responsive for the first 20 seconds, then gradually smooths from 0.5 to 0.125
        if( secsGone > 20.0 ) alpha -= 0.375 * progress;

        m_avgSecsPerFrame = (1.0 - alpha) * m_avgSecsPerFrame + alpha * secsPerFrame;
    }

    // Compute remaining time
    double secsRemaining = std::max(0.0, m_avgSecsPerFrame * framesToDo);

    quint64 duration = static_cast<quint64>(secsRemaining + 0.5);

    quint64 seconds = duration % 60;
    duration /= 60;
    quint64 minutes = duration % 60;
    duration /= 60;
    quint64 hours = duration;

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
