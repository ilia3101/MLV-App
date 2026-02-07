/*!
 * \file Histogram.h
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB histogram for an image
 */

#include "Histogram.h"
#include "math.h"
#include <QPainter>

#define HEIGHT 140
#define WIDTH 511 //511 = 8bit * 2 - 1

//Constructor
Histogram::Histogram()
{
    m_pHistogram = new QImage( WIDTH, HEIGHT, QImage::Format_RGB888 );
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
    uint32_t highestVal = 1;
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

        //"Real" points
        for( uint8_t y = 0; y < HEIGHT; y++ )
        {
            QColor color = QColor( Qt::black );
            if( tableR[x] >= ( uint32_t )( HEIGHT - y ) ) color.setRed( 255 );
            if( tableG[x] >= ( uint32_t )( HEIGHT - y ) ) color.setGreen( 255 );
            if( tableB[x] >= ( uint32_t )( HEIGHT - y ) ) color.setBlue( 255 );
            m_pHistogram->setPixelColor( x * 2, y, color );
        }

        //Interpolation
        if( x < 255 )
        {
            for( uint8_t y = 0; y < HEIGHT; y++ )
            {
                QColor color = QColor( Qt::black );
                if( ( ( tableR[x] + tableR[x-1] ) >> 1 ) >= ( uint32_t )( HEIGHT - y ) ) color.setRed( 255 );
                if( ( ( tableG[x] + tableG[x-1] ) >> 1 ) >= ( uint32_t )( HEIGHT - y ) ) color.setGreen( 255 );
                if( ( ( tableB[x] + tableB[x-1] ) >> 1 ) >= ( uint32_t )( HEIGHT - y ) ) color.setBlue( 255 );
                m_pHistogram->setPixelColor( ( x * 2 ) - 1, y, color );
            }
        }
    }
    return *m_pHistogram;
}

//Make histogram from Raw Image (8bit R, 8bit G, 8bit B,...)
QImage Histogram::getHistogramFromRaw( uint8_t *raw, uint16_t width, uint16_t height, bool under, bool over )
{
    uint32_t tableR[256] = {};
    uint32_t tableG[256] = {};
    uint32_t tableB[256] = {};

    // --- Count ---
    const uint8_t* p = raw;
    const uint32_t pixels = uint32_t(width) * height;
    for (uint32_t i = 0; i < pixels; ++i)
    {
        tableR[*p++]++;
        tableG[*p++]++;
        tableB[*p++]++;
    }

    // --- Scale + max ---
    uint32_t highestVal = 1;
    for (int x = 0; x < 256; ++x)
    {
        if (tableR[x]) tableR[x] = 100.0 * log(tableR[x]) + tableR[x] * 0.1;
        if (tableG[x]) tableG[x] = 100.0 * log(tableG[x]) + tableG[x] * 0.1;
        if (tableB[x]) tableB[x] = 100.0 * log(tableB[x]) + tableB[x] * 0.1;

        if (x > 2 && x < 253)
            highestVal = std::max({ highestVal, tableR[x], tableG[x], tableB[x] });
    }

    // --- Normalize ---
    for (int x = 0; x < 256; ++x)
    {
        tableR[x] = tableR[x] * HEIGHT / highestVal;
        tableG[x] = tableG[x] * HEIGHT / highestVal;
        tableB[x] = tableB[x] * HEIGHT / highestVal;
    }

    // --- Clear ---
    m_pHistogram->fill(Qt::black);

    QPainter painter(m_pHistogram);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setPen(Qt::NoPen);

    auto drawChannel = [&](const uint32_t* table, const QColor& color)
    {
        QPolygonF poly;
        poly.reserve(258);

        // bottom-left
        poly << QPointF(0, HEIGHT);

        for (int x = 0; x < 256; ++x)
        {
            const qreal px = x * 2.0;
            const qreal py = HEIGHT - table[x];
            poly << QPointF(px, py);
        }

        // bottom-right
        poly << QPointF(WIDTH, HEIGHT);

        painter.setBrush(color);
        painter.drawPolygon(poly);
    };

    drawChannel(tableR, Qt::red);
    drawChannel(tableG, Qt::green);
    drawChannel(tableB, Qt::blue);

    painter.end();

    // --- Over / Underexposed Marker (optional, bleibt wie gehabt) ---
    if (over || under)
    {
        QPainter p2(m_pHistogram);
        p2.setPen(Qt::NoPen);

        if (over)
        {
            p2.setBrush(Qt::red);
            p2.drawRect(WIDTH - 10, 0, 10, 10);
        }
        if (under)
        {
            p2.setBrush(Qt::blue);
            p2.drawRect(0, 0, 10, 10);
        }
    }

    return *m_pHistogram;
}
