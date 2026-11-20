/*!
 * \file WaveFormMonitor.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB WaveForm Monitor for an image
 */

#include <cmath>
#include "WaveFormMonitor.h"

QImage WaveFormMonitor::getWaveFormMonitorFromRaw( const uint8_t *m_pRawImage, uint16_t width, uint16_t height, uint16_t labelWidth, uint16_t labelHeight, bool parade )
{
    QImage image( std::min( labelWidth, width ), 256, QImage::Format_RGB32 );

    float waveWidth = parade ? image.width() / 3.0f : image.width();
    float widthFactor = waveWidth / width;

    // Skipping lines for performance
    int skipX = width / waveWidth;
    int skipY = std::max( height / 1024.0 + 0.5, 1.0 );

    float tableR[256] = {0};
    float tableG[256] = {0};
    float tableB[256] = {0};

    float logFactor = 0.25f;
    float lightnessFactor = 0.75f;

    // Intensity factor, maybe make it a parameter one day…
    float intensity = skipY;
    if( parade ) intensity *= ( 0.5f + 1 - lightnessFactor ) / 2;

    for( uint16_t x = 0; x < width; x += skipX )
    {
        uint16_t x1 = x & 1 ? x - 1 : x;

        // float rMax = 0;
        // float gMax = 0;
        // float bMax = 0;

        // Sum the columns
        for( uint16_t y = 0; y < height; y += skipY )
        {
            std::size_t i = ( x1 + width * y ) * 3;

            uint8_t r = m_pRawImage[ i ];
            uint8_t g = m_pRawImage[ i + 1 ];
            uint8_t b = m_pRawImage[ i + 2 ];

            tableR[r] = std::min( tableR[r] + intensity, 64.0f );
            tableG[g] = std::min( tableG[g] + intensity, 64.0f );
            tableB[b] = std::min( tableB[b] + intensity, 64.0f );

            // if( tableR[r] > rMax ) rMax = tableR[r];
            // if( tableG[g] > gMax ) gMax = tableG[g];
            // if( tableB[b] > bMax ) bMax = tableB[b];
        }

        // float max = std::max({ rMax, gMax, bMax });
        float max = 64.0f;
        float scale = 255.0f / std::log1p( logFactor * max );

        // Normalize
        for( int i = 0; i < 256; i++ )
        {
            tableR[i] = scale * std::log1p( logFactor * tableR[i] );
            tableG[i] = scale * std::log1p( logFactor * tableG[i] );
            tableB[i] = scale * std::log1p( logFactor * tableB[i] );
        }

        float x2 = x * widthFactor;

        for( int y = 0; y <= 255; y++ )
        {
            QRgb *line = reinterpret_cast<QRgb*>( image.scanLine( y ) );

            int i = 255 - y;

            // Paint
            if( parade )
            {
                line[int( x2 )] = QColor::fromHsl( 0, 255, tableR[i] * lightnessFactor ).rgb();
                line[int( x2 + waveWidth )] = QColor::fromHsl( 120, 255, tableG[i] * lightnessFactor ).rgb();
                line[int( x2 + waveWidth * 2 )] = QColor::fromHsl( 210, 255, tableB[i] * lightnessFactor ).rgb();
            }
            else
            {
                // QColor c( tableR[i], tableG[i], tableB[i] );
                // line[int( x2 )] = QColor::fromHsl( c.hue(), c.saturation(), std::max({ float(c.lightness()), tableR[i] * lightnessFactor, tableG[i] * lightnessFactor, tableB[i] * lightnessFactor }) ).rgb();
                line[int( x2 )] = qRgb( tableR[i], tableG[i], tableB[i] );
            }

            // Reset
            tableR[i] = tableG[i] = tableB[i] = 0;
        }
    }

    return image.scaled( labelWidth, labelHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
}
