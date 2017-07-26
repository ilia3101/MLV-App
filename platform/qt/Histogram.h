#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QImage>
#include <QVector>
#include <QRgb>

class Histogram
{

public:
    Histogram();
    QImage getHistogramFromImg( QImage img );

private:
    QImage *m_pHistogram;
};

#endif // HISTOGRAM_H
