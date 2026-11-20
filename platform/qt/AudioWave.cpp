/*!
 * \file AudioWave.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Paint audio track
 */

#include <QDebug>
#include <QLinearGradient>
#include <QPainter>
#include <cmath>
#include "AudioWave.h"
#include "math.h"

//Make a image of the audio track - as mono, mirror negative part to positive part
QImage AudioWave::getMonoWave(int16_t *pAudioTrack, uint64_t audioSize, uint16_t width, uint16_t height, qreal pixelRatio)
{
    if( width == 0 || height == 0 ) return QImage();

    width *= pixelRatio;
    height *= pixelRatio;

    QImage audioWave = QImage( width, height, QImage::Format_RGB32 );

    //Background with gradient
    QRect rect( 0, 0, width, height );
    QPainter painter( &audioWave );

    QLinearGradient gradient( rect.topLeft(), rect.bottomLeft() / 2 );
    gradient.setColorAt( 0, QColor::fromHsl( 150, 32, 112 ) );
    gradient.setColorAt( 1, QColor::fromHsl( 150, 64, 56 ) );
    painter.fillRect( rect, gradient );

    //If no data -> no wave -> return
    if( pAudioTrack == NULL ) return audioWave;

    static QVector<QRgb> colorLUT;

    if( colorLUT.size() != height )
    {
        colorLUT.resize( height );

        // Define gradient stops
        struct ColorStop { float t; QColor color; };

        QVector<ColorStop> stops = {
            { 0.24f, QColor( 0, 255, 96 ) },
            { 0.62f, QColor( 255, 255, 96 ) },
            { 1.0f, QColor( 255, 0, 96 ) }
        };

        for( int y = 0; y < height; y++ )
        {
            float t = static_cast<float>( y ) / ( height - 1 );

            float logFactor = 9.0f;
            t = 1.0f - log( 1.0f + ( 1.0f - t ) * logFactor ) / log( 1.0f + logFactor );

            // Find which gradient segment t belongs to
            const ColorStop* start = &stops[0];
            const ColorStop* end   = &stops.back();

            for( int i = 0; i < stops.size() - 1; i++ )
            {
                if( t >= stops[i].t && t <= stops[i + 1].t )
                {
                    start = &stops[i];
                    end   = &stops[i + 1];
                    break;
                }
            }

            // Interpolate RGB
            float segmentT = ( t - start->t ) / ( end->t - start->t );
            segmentT = qBound( 0.0f, segmentT, 1.0f );

            colorLUT[y] = qRgb(
                static_cast<int>( start->color.red()   + ( end->color.red()   - start->color.red() )   * segmentT ),
                static_cast<int>( start->color.green() + ( end->color.green() - start->color.green() ) * segmentT ),
                static_cast<int>( start->color.blue()  + ( end->color.blue()  - start->color.blue() )  * segmentT )
            );
        }
    }

    uint64_t totalSamples = audioSize / sizeof( int16_t );
    uint64_t samplesPerPixel = totalSamples / width;

    if( samplesPerPixel == 0 ) samplesPerPixel = 1;

    for( int x = 0; x < width; x++ )
    {
        uint64_t baseIndex = x * samplesPerPixel;
        uint64_t endIndex = std::min( baseIndex + samplesPerPixel, totalSamples );

        int peak = 0;

        for( uint64_t i = baseIndex; i < endIndex; i++ )
        {
            int absSample = std::abs( static_cast<int>( pAudioTrack[i] ) );
            if ( absSample > peak ) peak = absSample;
        }

        if (peak == 0) continue;

        float normalized = static_cast<float>( peak ) / 32768.0f;

        float logFactor = 90.0f;
        normalized = log( 1.0f + normalized * logFactor ) / log( 1.0f + logFactor );
        normalized = std::min( normalized, 1.0f );

        int yMax = static_cast<int>( normalized * height + 0.5f );

        for( int y = 0; y < yMax; y++ )
        {
            int drawY = height - 1 - y;
            QRgb *line = reinterpret_cast<QRgb*>( audioWave.scanLine( drawY ) );
            line[x] = colorLUT[y];
        }
    }

    return audioWave;
}
