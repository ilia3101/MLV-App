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
