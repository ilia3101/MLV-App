#include "Histogram.h"

Histogram::Histogram()
{
    m_pHistogram = new QImage( 256, 100, QImage::Format_RGB888 );
}

//Make histogram
QImage Histogram::getHistogramFromImg(QImage img)
{
    uint32_t tableR[256];
    uint32_t tableG[256];
    uint32_t tableB[256];
    //Initialize
    for( int i = 0; i < 255; i++ )
    {
        tableR[i] = 0;
        tableG[i] = 0;
        tableB[i] = 0;
    }
    //Count
    for( int y = 0; y < img.height(); y++ )
    {
        for( int x = 0; x < img.width(); x++ )
        {
            QColor color = img.pixelColor(x,y);
            tableR[color.red()]++;
            tableG[color.green()]++;
            tableB[color.blue()]++;
        }
    }
    //HighestVal
    uint32_t highestVal = 0;
    for( int i = 0; i < 255; i++ )
    {
        if( tableR[i] > highestVal ) highestVal = tableR[i];
        if( tableG[i] > highestVal ) highestVal = tableG[i];
        if( tableB[i] > highestVal ) highestVal = tableB[i];
    }
    //Normalize to 100 and Paint
    m_pHistogram->fill( Qt::black );
    for( int x = 0; x < 255; x++ )
    {
        tableR[x] = tableR[x] * 100 / highestVal;
        tableG[x] = tableG[x] * 100 / highestVal;
        tableB[x] = tableB[x] * 100 / highestVal;

        for( uint32_t y = 0; y < 100; y++ )
        {
            QColor color = QColor( Qt::black );
            if( tableR[x] > 100 - y ) color.setRed( 255 );
            if( tableG[x] > 100 - y ) color.setGreen( 255 );
            if( tableB[x] > 100 - y ) color.setBlue( 255 );
            m_pHistogram->setPixelColor( x, y, color );
        }
    }
    return *m_pHistogram;
}
