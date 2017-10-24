/*!
 * \file NoScrollSlider.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A slider which can't scroll
 */

#include "NoScrollSlider.h"

NoScrollSlider::NoScrollSlider(QWidget *parent)
    : QSlider(parent) {

}

void NoScrollSlider::wheelEvent(QWheelEvent *)
{
    //Do nothing!!!
}
