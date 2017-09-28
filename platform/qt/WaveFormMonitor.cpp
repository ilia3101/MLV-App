/*!
 * \file WaveFormMonitor.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB WaveForm Monitor for an image
 */

#include "WaveFormMonitor.h"

//The higher this values, the higher the performance
//The lower this values, the higher the quality
//We skip only columns, because it is really ugly if not...
#define MERGE 16 //must be 2^x

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

//Make waveform monitor from Raw Image (8bit R, 8bit G, 8bit B,...)
QImage WaveFormMonitor::getWaveFormMonitorFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height)
{
    double factor = 10.0; //Intensity Factor, maybe make it a parameter one day...

    uint32_t tableR[256] = {0};
    uint32_t tableG[256] = {0};
    uint32_t tableB[256] = {0};

    for( int x = 0; x < width; x = x + MERGE )
    {
        //Sum the columns
        for( int y = 0; y < height; y++ )
        {
            //Merging and skipping lines for performance
            tableR[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 0 ] ]++;
            tableG[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 1 ] ]++;
            tableB[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 2 ] ]++;
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

//Make Parade from Raw Image (8bit R, 8bit G, 8bit B,...)
QImage WaveFormMonitor::getParadeFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height)
{
    double factor = 10.0; //Intensity Factor, maybe make it a parameter one day...

    uint32_t tableR[256] = {0};
    uint32_t tableG[256] = {0};
    uint32_t tableB[256] = {0};

    for( int x = 0; x < width; x = x + MERGE + 3 )
    {
        //Sum the columns
        for( int y = 0; y < height; y++ )
        {
            //Merging and skipping lines for performance
            tableR[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 0 ] ]++;
            tableG[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 1 ] ]++;
            tableB[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 2 ] ]++;
        }

        for( uint16_t y = 0; y <= 255; y++ )
        {
            //Paint
            QColor color = QColor( Qt::black );
            if( ( tableR[255 - y] * factor ) > 255 ) color.setRed( 255 );
            else color.setRed( tableR[255 - y] * factor );
            m_pWaveForm->setPixelColor( ( x / MERGE / 3 ), y, color );

            color = QColor( Qt::black );
            if( ( tableG[255 - y] * factor ) > 255 ) color.setGreen( 255 );
            else color.setGreen( tableG[255 - y] * factor );
            m_pWaveForm->setPixelColor( ( x / MERGE / 3 )+( width / MERGE / 3 ), y, color );

            color = QColor( Qt::black );
            if( ( tableB[255 - y] * factor ) > 255 ) color.setBlue( 255 );
            else color.setBlue( tableB[255 - y] * factor );
            m_pWaveForm->setPixelColor( ( x / MERGE / 3 )+( width * 2 / MERGE / 3 ), y, color );

            //Reset
            tableR[255 - y] = 0;
            tableG[255 - y] = 0;
            tableB[255 - y] = 0;
        }
    }
    return *m_pWaveForm;
}

