/*!
 * \file AudioWave.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Paint audio track
 */

#include <QDebug>
#include <QLinearGradient>
#include <QPainter>
#include "AudioWave.h"
#include "math.h"

//Constructor
AudioWave::AudioWave()
{
    m_pAudioWave = new QImage( 1000, 32, QImage::Format_RGB888 );

    //Calc colors for Track
    for( int x = 0; x < 32; x++ )
    {
        //red lut
        m_red[x] = 32 * x - 480;
        if( m_red[x] > 255 ) m_red[x] = 255;
        if( m_red[x] < 0 ) m_red[x] = 0;

        //green lut
        m_green[x] = -32 * x + 992;
        if( m_green[x] > 255 ) m_green[x] = 255;
        if( m_green[x] < 0 ) m_green[x] = 0;
    }
}

//Destructor
AudioWave::~AudioWave()
{
    delete m_pAudioWave;
}

//Make a image of the audio track - as mono, mirror negative part to positive part
QImage AudioWave::getMonoWave(int16_t *pAudioTrack, uint64_t audioSize, uint16_t width, int pixelRatio)
{
    if( width == 0 ) return *m_pAudioWave;
    delete m_pAudioWave;

    width *= pixelRatio;

    m_pAudioWave = new QImage( width, 32 * pixelRatio, QImage::Format_RGB888 );

    //Background with gradient
    QRect rect( 0, 0, width, 32 * pixelRatio );
    QPainter painter( m_pAudioWave );
    QLinearGradient gradient( rect.topLeft(), (rect.topLeft() + rect.bottomLeft() ) / 2 ); // diagonal gradient from top-left to bottom-right
    gradient.setColorAt( 0, QColor( 99, 120, 106, 255 ) );
    gradient.setColorAt( 1, QColor( 43, 74, 53, 255 ) );
    painter.fillRect(rect, gradient);

    //If no data -> no wave -> return
    if( pAudioTrack == NULL ) return *m_pAudioWave;

    uint64_t pointPackageSize = audioSize / (uint64_t)width / sizeof(int16_t);

    //For each point in the graphic
    for( uint64_t x = 0; x < (uint64_t)width; x++ )
    {
        int16_t y = 0;

        //pack samples to
        for( uint64_t i = 0; i < pointPackageSize; i++ )
        {
            //positive part
            if( pAudioTrack[ ( ( pointPackageSize * x ) + i ) ] > y )
            {
                y = pAudioTrack[ ( ( pointPackageSize * x ) + i ) ];
            }
            //negativ part (mirrored)
            else if( pAudioTrack[ ( ( pointPackageSize * x ) + i ) ] < -y )
            {
                y = -pAudioTrack[ ( ( pointPackageSize * x ) + i ) ];
            }
        }

        //Some funny math to make it nice at max height of 32 pixel
        y = ( 100.0 * log( y ) + y / 10.0 ) / 116 * pixelRatio;
        //And make it safe
        if( y > ( ( 32  * pixelRatio ) - 1 ) ) y = ( 32  * pixelRatio ) - 1;

        //Paint point
        for( int i = 0; i < y; i++ )
        {
            QColor color = QColor( m_red[(int)(i / pixelRatio)], m_green[(int)(i / pixelRatio)], 100, 255 );
            m_pAudioWave->setPixelColor( x, ( 32  * pixelRatio ) - 1 - i, color );
        }
    }

    return *m_pAudioWave;
}

