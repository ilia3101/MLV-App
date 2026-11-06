/*!
 * \file AudioWave.h
 * \author masc4ii
 * \copyright 2017
 * \brief Paint audio track
 */

#ifndef AUDIOWAVE_H
#define AUDIOWAVE_H

#include <QImage>
#include <stdint.h>

class AudioWave
{
public:
    QImage getMonoWave(int16_t *pAudioTrack, uint64_t audioSize, uint16_t width, uint16_t height, qreal pixelRatio );
};

#endif // AUDIOWAVE_H
