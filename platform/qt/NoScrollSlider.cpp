/*!
 * \file NoScrollSlider.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A slider which can't scroll
 */

#include "NoScrollSlider.h"
#include <QStyleOption>

NoScrollSlider::NoScrollSlider(QWidget *parent)
    : QSlider(parent) {

}

void NoScrollSlider::wheelEvent(QWheelEvent *event)
{
    // Propagate to the parent
    event->ignore();
}

void NoScrollSlider::mouseDoubleClickEvent(QMouseEvent *event) {
    QStyleOptionSlider option;
    initStyleOption( &option );

    QRect handleRect = style()->subControlRect( QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this );

    if( handleRect.contains(event->pos()) ) emit doubleClicked();
}
