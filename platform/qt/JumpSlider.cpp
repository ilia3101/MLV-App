/*!
 * \file JumpSlider.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A slider which directly jumps to a clicked position
 */

#include "JumpSlider.h"
#include <QStyleOptionSlider>

//Reimplement MousePressEvent -> Jump to clicked position
void JumpSlider::mousePressEvent(QMouseEvent *event)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    if( event->button() == Qt::LeftButton &&
        !sr.contains( event->pos() ) )
    {
        int newVal;
        if( orientation() == Qt::Vertical )
        {
           double halfHandleHeight = ( 0.5 * sr.height() ) + 0.5;
           int adaptedPosY = height() - event->y();
           if( adaptedPosY < halfHandleHeight )
                 adaptedPosY = halfHandleHeight;
           if( adaptedPosY > height() - halfHandleHeight )
                 adaptedPosY = height() - halfHandleHeight;
           double newHeight = ( height() - halfHandleHeight ) - halfHandleHeight;
           double normalizedPosition = ( adaptedPosY - halfHandleHeight )  / newHeight ;

           newVal = minimum() + ( maximum()-minimum() ) * normalizedPosition;
        }
        else
        {
            double halfHandleWidth = ( 0.5 * sr.width() ) + 0.5;
            int adaptedPosX = event->x();
            if ( adaptedPosX < halfHandleWidth )
                  adaptedPosX = halfHandleWidth;
            if ( adaptedPosX > width() - halfHandleWidth )
                  adaptedPosX = width() - halfHandleWidth;
            double newWidth = ( width() - halfHandleWidth ) - halfHandleWidth;
            double normalizedPosition = ( adaptedPosX - halfHandleWidth )  / newWidth ;

            newVal = minimum() + ( ( maximum()-minimum() ) * normalizedPosition );
        }

        if( invertedAppearance() )
            setValue( maximum() - newVal );
        else
            setValue( newVal );

        event->accept();
    }
    QSlider::mousePressEvent(event);
}

