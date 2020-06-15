/*!
 * \file DoubleClickLabel.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A doubleclickable QLabel
 */

#include "DoubleClickLabel.h"

DoubleClickLabel::DoubleClickLabel(QWidget* parent)
    : QLabel(parent) {

}

DoubleClickLabel::~DoubleClickLabel() {}

void DoubleClickLabel::mouseDoubleClickEvent(QMouseEvent*) {
    emit doubleClicked();
}

void DoubleClickLabel::enterEvent(QEvent *event)
{
    Q_UNUSED (event);
    if( this->isEnabled() ) setStyleSheet( "QLabel { color: rgb(255,154,50); }" );
}

void DoubleClickLabel::leaveEvent(QEvent *event)
{
    Q_UNUSED (event);
    setStyleSheet( "" );
}
