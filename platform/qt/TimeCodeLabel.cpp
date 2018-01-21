/*!
 * \file TimeCodeLabel.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Paint TimeCode Image
 */

#include "TimeCodeLabel.h"
#include <QRect>
#include <QPainter>
#include <QLinearGradient>
#include <QFontDatabase>
#include <QString>

//Constructor
TimeCodeLabel::TimeCodeLabel()
{
    m_tcImage = new QImage( 200, 30, QImage::Format_RGB888 );

    QFontDatabase::addApplicationFont( ":/Fonts/Fonts/DSEG7Modern-Regular.ttf" );
}

//Destructor
TimeCodeLabel::~TimeCodeLabel()
{
    delete m_tcImage;
}

//Get the image for frameNumber at clipFps
QImage TimeCodeLabel::getTimeCodeLabel(uint32_t frameNumber, float clipFps)
{
    QRect rect( 0, 0, 200, 30 );
    QPainter painterTc( m_tcImage );
    QLinearGradient gradient( rect.topLeft(), (rect.topLeft() + rect.bottomLeft() ) / 2 ); // diagonal gradient from top-left to bottom-right
    gradient.setColorAt( 0, QColor( 100, 100, 100, 255 ) );
    gradient.setColorAt( 1, QColor( 30, 30, 30, 255 ) );
    painterTc.fillRect(rect, gradient);

    int frame = (int) (frameNumber % (int)clipFps);
    frameNumber /= (int)clipFps;
    int seconds = (int) (frameNumber % 60);
    frameNumber /= 60;
    int minutes = (int) (frameNumber % 60);
    frameNumber /= 60;
    int hours = (int) (frameNumber);

    QFont font = QFont("DSEG7 Modern", 20, 1);
    painterTc.setPen( QPen( QColor( 220, 220, 220 ) ) );
    painterTc.setFont( font );
    painterTc.drawText( 20, 5, 190, 20, 0, QString( "%1 : %2 : %3 . %4" )
                      .arg( hours, 2, 10, QChar('0') )
                      .arg( minutes, 2, 10, QChar('0') )
                      .arg( seconds, 2, 10, QChar('0') )
                      .arg( frame, 2, 10, QChar('0') ) );

    return *m_tcImage;
}
