/*!
 * \file NoScrollComboBox.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief A Combobox which doesn't change item when scrolling
 */

#include "NoScrollComboBox.h"

NoScrollComboBox::NoScrollComboBox(QWidget *parent)
    : QComboBox(parent) {

}

void NoScrollComboBox::wheelEvent(QWheelEvent *)
{
    //Do nothing!!!
}
