/*!
 * \file JumpSlider.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A slider which directly jumps to a clicked position
 */

#include "JumpSlider.h"

JumpSlider::JumpSlider(QWidget *parent, Qt::WindowFlags f)
    : QSlider(parent)
{

}

JumpSlider::~JumpSlider()
{

}

//Reimplement MousePressEvent -> Jump to clicked position
void JumpSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (orientation() == Qt::Vertical)
            setValue(minimum() + ((maximum()-minimum()) * (height()-event->y())) / height() ) ;
        else
            setValue(minimum() + ((maximum()-minimum()) * event->x()) / width() ) ;

        event->accept();
    }
    QSlider::mousePressEvent(event);
}

