/*!
 * \file ColorWheel.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief A ColorWheel as QLabel
 */

#include "ColorWheel.h"
#include <QPainter>
#include <QDebug>
#include <QMouseEvent>

#define SIZE           300
#define HALFSIZE       (SIZE/2)
#define HANDLESIZE     24
#define HALFHANDLESIZE (HANDLESIZE/2)
#define BOARDERSIZE    (SIZE/6)

//Constructor
ColorWheel::ColorWheel(QWidget* parent)
    : QLabel(parent) {
    m_pImage = new QImage( SIZE, SIZE+BOARDERSIZE, QImage::Format_RGBA8888 );
    m_cursor = QPoint( 0, 0 );
    m_yaw = 0;
    m_cursorSelected = false;
    m_wheelSelected = false;
    paintElement();
}

//Destructor
ColorWheel::~ColorWheel()
{
    delete m_pImage;
}

//Paint the colorwheel
void ColorWheel::paintElement()
{
    m_pImage->fill( QColor( 0, 0, 0, 0 ) );
    QPainter painterTc( m_pImage );
    painterTc.setRenderHint(QPainter::Antialiasing);

    //Color
    QConicalGradient gradient1( HALFSIZE, HALFSIZE, 105 );
    gradient1.setColorAt( 0, QColor( 255, 0, 0, 255 ) );
    gradient1.setColorAt( 0.18, QColor( 255, 255, 0, 255 ) );
    gradient1.setColorAt( 0.375, QColor( 0, 255, 0, 255 ) );
    gradient1.setColorAt( 0.5, QColor( 0, 255, 255, 255 ) );
    gradient1.setColorAt( 0.68, QColor( 0, 0, 255, 255 ) );
    gradient1.setColorAt( 0.875, QColor( 255, 0, 255, 255 ) );
    gradient1.setColorAt( 1, QColor( 255, 0, 0, 255 ) );

    painterTc.setBrush( gradient1 );
    painterTc.drawEllipse( 0, 0, SIZE, SIZE );

    //Black center
    QRadialGradient gradient2( HALFSIZE, HALFSIZE, HALFSIZE, HALFSIZE, HALFSIZE );
    gradient2.setColorAt( 0.8, QColor( 0, 0, 0, 255 ) );
    gradient2.setColorAt( 0.95, QColor( 0, 0, 0, 0 ) );
    painterTc.setBrush( gradient2 );
    painterTc.drawEllipse( 0, 0, SIZE, SIZE );

    //Glass
    QRadialGradient gradient3( HALFSIZE, HALFSIZE, HALFSIZE, HALFSIZE, HALFSIZE/2 );
    gradient3.setColorAt( 0.6, QColor( 100, 100, 100, 50 ) );
    gradient3.setColorAt( 1, QColor( 0, 0, 0, 50 ) );
    painterTc.setBrush( gradient3 );
    painterTc.drawEllipse( 0, 0, SIZE, SIZE );

    //Lines
    painterTc.setPen( QColor( 100, 100, 100, 255 ) );
    painterTc.drawLine( HALFSIZE-1, BOARDERSIZE, HALFSIZE-1, SIZE-BOARDERSIZE );
    painterTc.drawLine( HALFSIZE, BOARDERSIZE, HALFSIZE, SIZE-BOARDERSIZE );
    painterTc.drawLine( BOARDERSIZE, HALFSIZE-1, SIZE-BOARDERSIZE, HALFSIZE-1 );
    painterTc.drawLine( BOARDERSIZE, HALFSIZE, SIZE-BOARDERSIZE, HALFSIZE );

    //Marker
    painterTc.drawPixmap( m_cursor.x() + HALFSIZE - HALFHANDLESIZE, m_cursor.y() + HALFSIZE - HALFHANDLESIZE,
                          HANDLESIZE, HANDLESIZE,
                          QPixmap( "://darkstyle/SliderHandle.png" ).scaled( HANDLESIZE, HANDLESIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );

    //Reset Icon
    painterTc.drawPixmap( SIZE-32-1, 0,
                          32, 32,
                          QPixmap( ":/RetinaIMG/RetinaIMG/Reload-icon.png" ).scaled( 64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );

    //Y wheel
    QLinearGradient gradient4( 0, 0, SIZE, 0 );
    gradient4.setColorAt( 0, QColor( 50, 50, 50, 255 ) );
    gradient4.setColorAt( 0.5, QColor( 100, 100, 100, 255 ) );
    gradient4.setColorAt( 1, QColor( 50, 50, 50, 255 ) );
    painterTc.setPen( QColor( 0, 0, 0, 255 ) );
    painterTc.setBrush( gradient4 );
    painterTc.drawRect( 0, SIZE+BOARDERSIZE/2, SIZE, SIZE+BOARDERSIZE );
    //Y wheel ripple
    QLinearGradient gradient5( 0, 0, SIZE, 0 );
    gradient5.setColorAt( 0, QColor( 100, 100, 100, 255 ) );
    gradient5.setColorAt( 0.5, QColor( 100, 150, 100, 255 ) );
    gradient5.setColorAt( 1, QColor( 100, 100, 100, 255 ) );
    for( int i = 0; i < 10; i++ )
    {
        painterTc.drawRect( (m_yaw % (SIZE/10))+(i*SIZE/10), SIZE+BOARDERSIZE/2, SIZE/10, SIZE+BOARDERSIZE );
    }
    painterTc.drawLine( 0, SIZE+BOARDERSIZE/2, 0, SIZE+BOARDERSIZE );
    painterTc.drawLine( SIZE-1, SIZE+BOARDERSIZE/2, SIZE-1, SIZE+BOARDERSIZE );
    painterTc.drawLine( 0, SIZE+BOARDERSIZE-1, SIZE-1, SIZE+BOARDERSIZE-1 );

    //Paint to label
    QPixmap pic = QPixmap::fromImage( *m_pImage ).scaled( HALFSIZE * devicePixelRatio(), (HALFSIZE+BOARDERSIZE/2) * devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation );
    pic.setDevicePixelRatio( devicePixelRatio() );
    setPixmap( pic );
}

//Mouse pressed -> select marker
void ColorWheel::mousePressEvent(QMouseEvent *mouse)
{
    //Marker
    if( ( ( mouse->localPos().x() * devicePixelRatio() ) > ( HALFSIZE - HALFHANDLESIZE + m_cursor.x() ) * devicePixelRatio() / 2 )
     && ( ( mouse->localPos().x() * devicePixelRatio() ) < ( HALFSIZE + HALFHANDLESIZE + m_cursor.x() ) * devicePixelRatio() / 2 )
     && ( ( mouse->localPos().y() * devicePixelRatio() ) > ( HALFSIZE - HALFHANDLESIZE + m_cursor.y() ) * devicePixelRatio() / 2 )
     && ( ( mouse->localPos().y() * devicePixelRatio() ) < ( HALFSIZE + HALFHANDLESIZE + m_cursor.y() ) * devicePixelRatio() / 2 ) )
    {
        m_cursorSelected = true;
    }
    //Reset
    if( mouse->localPos().x() > ( ( SIZE - 32 ) / 2 )
     && mouse->localPos().y() < ( 32 / 2 ) )
    {
        m_cursor = QPoint( 0, 0 );
        m_yaw = 0;
        paintElement();
    }
    //Wheel
    if( mouse->localPos().y() > ( SIZE + BOARDERSIZE/2 ) / 2 )
    {
        m_wheelSelected = true;
    }
}

//Mouse released -> deselect marker
void ColorWheel::mouseReleaseEvent(QMouseEvent *)
{
    m_cursorSelected = false;
    m_wheelSelected = false;
}

//Mouse moved -> move marker
void ColorWheel::mouseMoveEvent(QMouseEvent *mouse)
{
    //move the marker
    if( m_cursorSelected )
    {
        //Limit of marker
        if( ( ( mouse->localPos().x() - HALFSIZE / 2 )
            * ( mouse->localPos().x() - HALFSIZE / 2 )
            + ( mouse->localPos().y() - HALFSIZE / 2 )
            * ( mouse->localPos().y() - HALFSIZE / 2 ) )
                > ( ( ( HALFSIZE - BOARDERSIZE ) / 2 )
                  * ( ( HALFSIZE - BOARDERSIZE ) / 2 ) ) )
        {
            return;
        }

        //Marker positioning
        m_cursor.setX( mouse->localPos().x() * 2 - HALFSIZE );
        m_cursor.setY( mouse->localPos().y() * 2 - HALFSIZE );

        paintElement();
    }

    //turn the wheel
    if( m_wheelSelected )
    {
        m_yaw = mouse->localPos().x() * 2 - HALFSIZE;
        paintElement();
    }
}
