/*!
 * \file WaveFormMonitor.h
 * \author masc4ii
 * \copyright 2017
 * \brief Draws a RGB WaveForm Monitor for an image
 */

#ifndef WAVEFORMMONITOR_H
#define WAVEFORMMONITOR_H

#include <QImage>
#include <stdint.h>

class WaveFormMonitor
{
public:
    WaveFormMonitor( uint16_t width );
    ~WaveFormMonitor();
    QImage getWaveFormMonitorFromRaw( uint8_t *m_pRawImage, uint16_t width, uint16_t height );
    QImage getParadeFromRaw( uint8_t *m_pRawImage, uint16_t width, uint16_t height );

private:
    QImage *m_pWaveForm;
};

#endif // WAVEFORMMONITOR_H
