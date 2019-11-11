/*!
 * \file NoScrollSpinBox.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief A Spinbox which doesn't change item when scrolling
 */

#include "NoScrollSpinBox.h"

NoScrollSpinBox::NoScrollSpinBox(QWidget *parent)
    : QSpinBox(parent) {

}

void NoScrollSpinBox::wheelEvent(QWheelEvent *)
{
    //Do nothing!!!
}
