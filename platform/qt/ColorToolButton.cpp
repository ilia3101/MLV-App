/*!
 * \file ColorToolButton.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A QToolbutton which changes the color when checked
 */

#include "ColorToolButton.h"

//Constructor
ColorToolButton::ColorToolButton(QWidget *parent)
    : QToolButton( parent ) {
    connect( this, SIGNAL(toggled(bool)), this, SLOT(buttonChecked(bool)) );
    m_palette = this->palette();
}

//Change color for button in dependency if checked or not
void ColorToolButton::buttonChecked(bool on)
{
    if( on )
    {
        QPalette palette = m_palette;
        palette.setColor(QPalette::Button,QColor(127,127,127));
        palette.setColor(QPalette::ButtonText,Qt::white);
        palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(100,100,100));
        this->setPalette( palette );
    }
    else
    {
        this->setPalette( m_palette );
    }
}
