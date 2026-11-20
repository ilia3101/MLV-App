/*!
 * \file Histogram.h
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB histogram for an image
 */

#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QImage>
#include <stdint.h>

class Histogram
{
public:
    //QImage getHistogramFromImg( QImage *img );
    static QImage getHistogramFromRaw( const uint8_t *raw, uint16_t width, uint16_t height, uint16_t labelWidth, uint16_t labelHeight, bool under, bool over );
};

#endif // HISTOGRAM_H
