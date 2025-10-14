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
void StatusDialog::drawTimeFromToDoFrames(uint32_t frames)
{
    QDateTime currentTime = QDateTime::currentDateTime();
    quint64 secsGone = m_startTime.secsTo( currentTime );
    uint32_t framesGone = m_totalTodoFrames - frames;
    if( framesGone == 0 ) return; //we don't like to divide by 0
    quint64 secsTotalEstimated = m_totalTodoFrames * secsGone / framesGone;

    QDateTime estimatedFinalTime = currentTime.addSecs( secsTotalEstimated - secsGone );

    quint64 duration = currentTime.secsTo(estimatedFinalTime);
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
