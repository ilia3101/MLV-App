#include <QDebug>
#include "AudioWave.h"

//Constructor
AudioWave::AudioWave()
{
    m_pAudioWave = new QImage( 1000, 32, QImage::Format_RGB888 );
}

//Destructor
AudioWave::~AudioWave()
{
    delete m_pAudioWave;
}

//Make a image of the audio track - as mono
QImage AudioWave::getMonoWave(uint16_t *pAudioTrack, uint64_t audioSize, uint16_t width)
{
    if( width == 0 ) return *m_pAudioWave;
    delete m_pAudioWave;
    m_pAudioWave = new QImage( width, 32, QImage::Format_RGB888 );
    m_pAudioWave->fill( Qt::darkGreen );

    if( pAudioTrack == NULL ) return *m_pAudioWave;

    uint64_t pointPackageSize = audioSize / (uint64_t)width / sizeof(uint16_t);

    //For each point in the graphic
    for( uint64_t x = 0; x < (uint64_t)width; x++ )
    {
        uint16_t y = 0;

        //pack samples to
        for( uint64_t i = 0; i < pointPackageSize; i++ )
        {
            if( pAudioTrack[ sizeof(uint16_t) * ( ( pointPackageSize * x ) + i ) ] > y )
            {
                y = pAudioTrack[ sizeof(uint16_t) * ( ( pointPackageSize * x ) + i ) ];
            }
        }

        //From 16bit to 5bit --> height of 32 pixel
        y = y >> 11;
        if( y > 31 ) y = 31;

        //qDebug() << "AudioTrackImageDebug:" << width << x << y << pointPackageSize << audioSize << sizeof(uint16_t) * ( ( pointPackageSize * x ) + pointPackageSize );

        //Paint point
        for( int i = 0; i < y; i++ )
        {
            m_pAudioWave->setPixelColor( x, 31 - i, Qt::green );
        }
    }

    return *m_pAudioWave;
}

