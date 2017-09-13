/*!
 * \file ResizeLabel.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A QLabel which is resizeable
 */

#include "ResizeLabel.h"

ResizeLabel::ResizeLabel(QWidget* parent, Qt::WindowFlags f)
    : QLabel(parent) {

}

ResizeLabel::~ResizeLabel() {}

void ResizeLabel::resizeEvent(QResizeEvent* event) {
    emit sizeChanged();
}

