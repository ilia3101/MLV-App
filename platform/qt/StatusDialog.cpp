/*!
 * \file StatusDialog.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Just a dialog with a progress bar
 */

#include "StatusDialog.h"
#include <QDebug>

//Constructor
StatusDialog::StatusDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StatusDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint );
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
}

//Draw remaining time to UI, input is todoFrames
void StatusDialog::drawTimeFromToDoFrames(uint32_t frames)
{
    QDateTime currentTime = QDateTime::currentDateTime();
    quint64 secsGone = m_startTime.secsTo( currentTime );
    uint32_t framesGone = m_totalTodoFrames - frames;
    quint64 secsTotalEstimated = m_totalTodoFrames * secsGone / framesGone;

    QDateTime estimatedFinalTime = currentTime.addSecs( secsTotalEstimated - secsGone );

    //qDebug() << "Estimated secs:" << (secsTotalEstimated - secsGone);
}

//Start Time for remaining time calculation
void StatusDialog::startExportTime()
{
    m_startTime = QDateTime::currentDateTime();
}

//Abort clicked
void StatusDialog::on_pushButtonAbort_clicked()
{
    emit abortPressed();
}
