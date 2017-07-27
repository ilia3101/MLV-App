/*!
 * \file WaveFormMonitor.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB WaveFormMonitor for an image
 */

#include "WaveFormMonitor.h"

//the higher these values, the higher the performance
//the lower these values, the higher the quality
#define MERGE 16 //must be 2^x
#define SKIP 16 //must be <= MERGE and be 2^x

//Constructor
WaveFormMonitor::WaveFormMonitor( uint16_t width )
{
    m_pWaveForm = new QImage( width / MERGE, 256, QImage::Format_RGB888 );
}

//Destructor
WaveFormMonitor::~WaveFormMonitor()
{
    delete m_pWaveForm;
}

//Make wave form monitor from Raw Image (8bit R, 8bit G, 8bit B,...)
QImage WaveFormMonitor::getWaveFormMonitorFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height)
{
    double factor = 10.0 * SKIP / MERGE;

    uint32_t tableR[256] = {0};
    uint32_t tableG[256] = {0};
    uint32_t tableB[256] = {0};

    for( int x = 0; x < width; x=x+2 )
    {
        //Sum the columns
        for( int y = 0; y < height; y++ )
        {
            //Merging and skipping lines for performance
            for( int i = 0; i < MERGE; i = i + SKIP )
            {
                tableR[ m_pRawImage[ ( ( x + i + ( width * y ) ) * 3 ) + 0 ] ]++;
                tableG[ m_pRawImage[ ( ( x + i + ( width * y ) ) * 3 ) + 1 ] ]++;
                tableB[ m_pRawImage[ ( ( x + i + ( width * y ) ) * 3 ) + 2 ] ]++;
            }
        }

        for( uint16_t y = 0; y <= 255; y++ )
        {
            //Paint
            QColor color = QColor( Qt::black );
            if( ( tableR[255 - y] * factor ) > 255 ) color.setRed( 255 );
            else color.setRed( tableR[255 - y] * factor );
            if( ( tableG[255 - y] * factor ) > 255 ) color.setGreen( 255 );
            else color.setGreen( tableG[255 - y] * factor );
            if( ( tableB[255 - y] * factor ) > 255 ) color.setBlue( 255 );
            else color.setBlue( tableB[255 - y] * factor );
            m_pWaveForm->setPixelColor( x / MERGE, y, color );

            //Reset
            tableR[255 - y] = 0;
            tableG[255 - y] = 0;
            tableB[255 - y] = 0;
        }
    }
    return *m_pWaveForm;
}

