/*!
 * \file Histogram.h
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB histogram for an image
 */

#include "Histogram.h"
#include "math.h"

#define HEIGHT 200

//Constructor
Histogram::Histogram()
{
    m_pHistogram = new QImage( 256, HEIGHT, QImage::Format_RGB888 );
}

//Destructor
Histogram::~Histogram()
{
    delete m_pHistogram;
}

//Make histogram from QImage
QImage Histogram::getHistogramFromImg( QImage *img )
{
    uint32_t tableR[256] = {0};
    uint32_t tableG[256] = {0};
    uint32_t tableB[256] = {0};

    //Count
    for( int y = 0; y < img->height(); y++ )
    {
        for( int x = 0; x < img->width(); x++ )
        {
            tableR[ img->pixelColor( x, y ).red() ]++;
            tableG[ img->pixelColor( x, y ).green() ]++;
            tableB[ img->pixelColor( x, y ).blue() ]++;
        }
    }
    //Highest Value
    uint32_t highestVal = 0;
    for( uint16_t x = 0; x <= 255; x++ )
    {
        //We scale something in between linear and log
        if( tableR[x] ) tableR[x] = 100.0 * log( tableR[x] ) + tableR[x] / 10.0;
        if( tableG[x] ) tableG[x] = 100.0 * log( tableG[x] ) + tableG[x] / 10.0;
        if( tableB[x] ) tableB[x] = 100.0 * log( tableB[x] ) + tableB[x] / 10.0;
        //and search the highest value
        if( x < 3 || x > 252 ) continue; //but do not normalize at the lowest or highest end
        if( tableR[x] > highestVal ) highestVal = tableR[x];
        if( tableG[x] > highestVal ) highestVal = tableG[x];
        if( tableB[x] > highestVal ) highestVal = tableB[x];
    }
    //Normalize to 100 and Paint
    m_pHistogram->fill( Qt::black );
    for( uint16_t x = 0; x <= 255; x++ )
    {
        tableR[x] = tableR[x] * HEIGHT / highestVal;
        tableG[x] = tableG[x] * HEIGHT / highestVal;
        tableB[x] = tableB[x] * HEIGHT / highestVal;

        for( uint8_t y = 0; y < HEIGHT; y++ )
        {
            QColor color = QColor( Qt::black );
            if( tableR[x] >= HEIGHT - y ) color.setRed( 255 );
            if( tableG[x] >= HEIGHT - y ) color.setGreen( 255 );
            if( tableB[x] >= HEIGHT - y ) color.setBlue( 255 );
            m_pHistogram->setPixelColor( x, y, color );
        }
    }
    return *m_pHistogram;
}

//Make histogram from Raw Image (8bit R, 8bit G, 8bit B,...)
QImage Histogram::getHistogramFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height)
{
    uint32_t tableR[256] = {0};
    uint32_t tableG[256] = {0};
    uint32_t tableB[256] = {0};

    //Count
    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            tableR[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 0 ] ]++;
            tableG[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 1 ] ]++;
            tableB[ m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 2 ] ]++;
        }
    }
    //Highest Value
    uint32_t highestVal = 0;
    for( uint16_t x = 0; x <= 255; x++ )
    {
        //We scale something in between linear and log
        if( tableR[x] ) tableR[x] = 100.0 * log( tableR[x] ) + tableR[x] / 10.0;
        if( tableG[x] ) tableG[x] = 100.0 * log( tableG[x] ) + tableG[x] / 10.0;
        if( tableB[x] ) tableB[x] = 100.0 * log( tableB[x] ) + tableB[x] / 10.0;
        //and search the highest value
        if( x < 3 || x > 252 ) continue; //but do not normalize at the lowest or highest end
        if( tableR[x] > highestVal ) highestVal = tableR[x];
        if( tableG[x] > highestVal ) highestVal = tableG[x];
        if( tableB[x] > highestVal ) highestVal = tableB[x];
    }
    //Normalize to 100 and Paint
    m_pHistogram->fill( Qt::black );
    for( uint16_t x = 0; x <= 255; x++ )
    {
        tableR[x] = tableR[x] * HEIGHT / highestVal;
        tableG[x] = tableG[x] * HEIGHT / highestVal;
        tableB[x] = tableB[x] * HEIGHT / highestVal;

        for( uint8_t y = 0; y < HEIGHT; y++ )
        {
            QColor color = QColor( Qt::black );
            if( tableR[x] >= HEIGHT - y ) color.setRed( 255 );
            if( tableG[x] >= HEIGHT - y ) color.setGreen( 255 );
            if( tableB[x] >= HEIGHT - y ) color.setBlue( 255 );
            m_pHistogram->setPixelColor( x, y, color );
        }
    }
    return *m_pHistogram;
}
