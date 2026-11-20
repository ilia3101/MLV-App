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
    static QImage getWaveFormMonitorFromRaw( const uint8_t *m_pRawImage, uint16_t width, uint16_t height, uint16_t labelWidth, uint16_t labelHeight, bool parade = false );
};

#endif // WAVEFORMMONITOR_H
