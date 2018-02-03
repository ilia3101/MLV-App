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
    Histogram();
    ~Histogram();
    QImage getHistogramFromImg( QImage *img );
    QImage getHistogramFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height , bool under, bool over);

private:
    QImage *m_pHistogram;
};

#endif // HISTOGRAM_H
