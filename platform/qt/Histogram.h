#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QImage>

class Histogram
{

public:
    Histogram();
    QImage getHistogramFromImg( QImage img );
    QImage getHistogramFromRaw( uint8_t *m_pRawImage, uint16_t width, uint16_t height );

private:
    QImage *m_pHistogram;
};

#endif // HISTOGRAM_H
