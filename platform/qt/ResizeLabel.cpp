/*!
 * \file ResizeLabel.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A QLabel which is resizeable
 */

#include "ResizeLabel.h"

ResizeLabel::ResizeLabel(QWidget* parent)
    : QLabel(parent) {

}

ResizeLabel::~ResizeLabel() {}

void ResizeLabel::resizeEvent(QResizeEvent *) {
    emit sizeChanged();
}

