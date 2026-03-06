/*!
 * \file StatusDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a progress bar
 */

#include "StatusDialog.h"
#include <QDebug>
#include <QKeyEvent>
#include <QFontDatabase>

//Constructor
StatusDialog::StatusDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StatusDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint );

    // Use a monospace font for the labelEstimatedTime to prevent text "dancing" :)
    QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui->labelEstimatedTime->setFont(monospace);
}

//Destructor
StatusDialog::~StatusDialog()
{
    delete ui;
}

//Export initialization
void StatusDialog::exportStart(int numberOfJobs, uint32_t totalFrames)
{
    m_totalTodoFrames = totalFrames;
    m_startTime = QDateTime::currentDateTime();
    m_isLoopRunning = false;
    m_paused = false;

    ui->totalProgressBar->setMaximum( totalFrames );
    ui->totalProgressBar->setValue( 0 );

    if( numberOfJobs > 1)
    {
        ui->totalProgressBar->show();
    }
    else
    {
        ui->totalProgressBar->hide();
    }

    ui->labelEstimatedTime->setText( "Estimating remaining time…" );
    ui->pushButtonPause->setText( "Pause" );
    ui->pushButtonPause->hide();

    this->layout()->activate();
    this->adjustSize();
}

//Set job/clip frames
void StatusDialog::setJobFrames(uint32_t jobFrames)
{
    m_jobFrames = jobFrames;
    m_jobFramesDone = -1;
}

//Get formatted time string
QString StatusDialog::getTimeString(double secsRemaining)
{
    quint64 duration = static_cast<quint64>( secsRemaining + 0.5 );

    quint64 seconds = duration % 60;
    duration /= 60;
    quint64 minutes = duration % 60;
    duration /= 60;
    quint64 hours = duration;

    return QString( "%1:%2:%3" )
                .arg( hours, 2, 10, QChar('0') )
                .arg( minutes, 2, 10, QChar('0') )
                .arg( seconds, 2, 10, QChar('0') );
}

//Draw remaining time to UI, input is todoFrames
void StatusDialog::drawTimeFromToDoFrames(uint32_t framesToDo)
{
    if( framesToDo < 1 || m_jobFrames < 1 ) return;

    static uint32_t prevFamesToDo = 0;

    double secsGone = 0;
    double secsPerFrame = 0;
    static double avgSecsPerFrame = 0;

    if( m_jobFramesDone == -1 )
    {
        m_jobStartTime = QDateTime::currentDateTime();
        avgSecsPerFrame = 0;
        prevFamesToDo = framesToDo;
    }

    m_jobFramesDone = prevFamesToDo - framesToDo;

    if ( m_jobFramesDone < 1 ) return;

    QDateTime currentTime = QDateTime::currentDateTime();

    secsGone = m_jobStartTime.msecsTo( currentTime ) / 1000.0;
    secsGone = std::max( 0.0, secsGone );

    secsPerFrame = secsGone / m_jobFramesDone;

    if( avgSecsPerFrame <= 0.0 )
    {
        avgSecsPerFrame = secsPerFrame;
    }

    // Adaptive smoothing (better accuracy with less jitter)
    // Gradually smooths from 0.5 to 0.125 with every job/clip
    double progress = static_cast<double>( m_jobFramesDone ) / m_jobFrames;
    double alpha = 0.5 - ( 0.375 * progress );

    avgSecsPerFrame = ( 1.0 - alpha ) * avgSecsPerFrame + alpha * secsPerFrame;

    // Compute remaining and elapsed time
    double jobSecsRemaining = avgSecsPerFrame * ( m_jobFrames - m_jobFramesDone );
    jobSecsRemaining = std::max( 0.0, jobSecsRemaining );
    double jobSecsElapsed = m_jobStartTime.msecsTo( currentTime ) / 1000.0;
    jobSecsElapsed = std::max( 0.0, jobSecsElapsed );

    if( ui->totalProgressBar->isVisible() )
    {
        double secsRemaining = avgSecsPerFrame * framesToDo;
        secsRemaining = std::max( 0.0, secsRemaining );
        double secsElapsed = m_startTime.msecsTo( currentTime ) / 1000.0;
        secsElapsed = std::max( 0.0, secsElapsed );

        ui->labelEstimatedTime->setText(
            QString( "%1 Remaining / %2 Elapsed (Current)\n%3 Remaining / %4 Elapsed (Total)" )
                .arg( getTimeString( jobSecsRemaining ) )
                .arg( getTimeString( jobSecsElapsed ) )
                .arg( getTimeString( secsRemaining ) )
                .arg( getTimeString( secsElapsed ) )
        );
    }
    else
    {
        ui->labelEstimatedTime->setText(
            QString( "%1 Remaining / %2 Elapsed" )
                .arg( getTimeString( jobSecsRemaining ) )
                .arg( getTimeString( jobSecsElapsed ) )
        );
    }
}

void StatusDialog::totalProgressBar(uint32_t framesToDo)
{
    if( ui->totalProgressBar->isVisible() )
    {
        ui->totalProgressBar->setValue( m_totalTodoFrames - framesToDo );
    }
}

bool StatusDialog::isPaused()
{
    return m_paused;
}

//Toggle pause/resume
void StatusDialog::togglePauseResume( int state )
{
    QString label = " (Paused)";

    // Resume
    if( state )
    {
        // Allow resuming only after the parallel loop has finished (prevents double-clicks on the pause button)
        if( m_isLoopRunning ) return;

        if( m_paused )
        {
            m_paused = false;

            // Subtract paused time from start time
            QDateTime currentTime = QDateTime::currentDateTime();
            quint64 mSeconds = m_pausedTime.msecsTo( currentTime );

            m_startTime = m_startTime.addMSecs( mSeconds );
            m_jobStartTime = m_jobStartTime.addMSecs( mSeconds );

            ui->label->setText( ui->label->text().replace( label, "" ) );
            ui->pushButtonPause->setText( "Pause" );
        }

        if( state == 2 ) emit resumePressed();
    }
    // Pause
    else if( !m_paused )
    {
        m_paused = true;
        m_pausedTime = QDateTime::currentDateTime();

        ui->label->setText( ui->label->text().append( label ) );
        ui->pushButtonPause->setText( "Resume" );
    }
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

//Pause clicked
void StatusDialog::on_pushButtonPause_clicked()
{
    togglePauseResume( m_paused ? 2 : 0 );
}

//Abort clicked
void StatusDialog::on_pushButtonAbort_clicked()
{
    emit abortPressed();
}
