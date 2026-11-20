/*!
 * \file Histogram.h
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB histogram for an image
 */

#include "Histogram.h"
#include "math.h"
#include <QPainter>

//Make histogram from QImage
/*
QImage Histogram::getHistogramFromImg( QImage *img )
{
    uint32_t tableR[256] = {0};
    uint32_t tableG[256] = {0};
    uint32_t tableB[256] = {0};

    double factor = WIDTH / 256.0;

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
            QColor color = Qt::black;
            if( tableR[x] >= ( uint32_t )( HEIGHT - y ) ) color.setRed( 255 );
            if( tableG[x] >= ( uint32_t )( HEIGHT - y ) ) color.setGreen( 255 );
            if( tableB[x] >= ( uint32_t )( HEIGHT - y ) ) color.setBlue( 255 );
            m_pHistogram->setPixelColor( x * factor, y, color );
        }

        //Interpolation
        if( x < 255 )
        {
            for( uint8_t y = 0; y < HEIGHT; y++ )
            {
                QColor color = Qt::black;
                if( ( ( tableR[x] + tableR[x-1] ) >> 1 ) >= ( uint32_t )( HEIGHT - y ) ) color.setRed( 255 );
                if( ( ( tableG[x] + tableG[x-1] ) >> 1 ) >= ( uint32_t )( HEIGHT - y ) ) color.setGreen( 255 );
                if( ( ( tableB[x] + tableB[x-1] ) >> 1 ) >= ( uint32_t )( HEIGHT - y ) ) color.setBlue( 255 );
                m_pHistogram->setPixelColor( ( x * factor ) - 1, y, color );
            }
        }
    }
    return *m_pHistogram;
}
*/

//Make histogram from Raw Image (8bit R, 8bit G, 8bit B,...)
QImage Histogram::getHistogramFromRaw( const uint8_t *raw, uint16_t width, uint16_t height, uint16_t labelWidth, uint16_t labelHeight, bool under, bool over )
{
    QImage image( labelWidth, labelHeight, QImage::Format_RGB32 );

    uint32_t tableR[256] = {};
    uint32_t tableG[256] = {};
    uint32_t tableB[256] = {};

    double factor = image.width() / 256.0;

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
        tableR[x] = tableR[x] * image.height() / highestVal;
        tableG[x] = tableG[x] * image.height() / highestVal;
        tableB[x] = tableB[x] * image.height() / highestVal;
    }

    // --- Clear ---
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setPen(Qt::NoPen);

    auto drawChannel = [&](const uint32_t* table, const QColor& color)
    {
        QPolygonF poly;
        poly.reserve(258);

        // bottom-left
        poly << QPointF(0, image.height());

        for (int x = 0; x < 256; ++x)
        {
            const qreal px = x * factor;
            const qreal py = image.height() - table[x];
            poly << QPointF(px, py);
        }

        // bottom-right
        poly << QPointF(image.width(), image.height());

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
        QPainter p2(&image);
        p2.setPen(Qt::NoPen);

        int size = image.width() * 0.025;

        if (over)
        {
            p2.setBrush(Qt::white);
            p2.drawRect(image.width() - (size + 1), 0, size + 1, size + 1);
            p2.setBrush(Qt::red);
            p2.drawRect(image.width() - size, 0, size, size);
        }
        if (under)
        {
            p2.setBrush(Qt::white);
            p2.drawRect(0, 0, size + 1, size + 1);
            p2.setBrush(Qt::blue);
            p2.drawRect(0, 0, size, size);
        }
    }

    return image;
}
