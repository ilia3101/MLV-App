/*!
 * \file AudioWave.h
 * \author masc4ii
 * \copyright 2017
 * \brief Paint audio track
 */

#ifndef AUDIOWAVE_H
#define AUDIOWAVE_H

#include <QImage>

class AudioWave
{
public:
    AudioWave();
    ~AudioWave();
    QImage getMonoWave(int16_t *pAudioTrack, uint64_t audioSize, uint16_t width, int pixelRatio );

private:
    QImage *m_pAudioWave;
    int m_red[32];
    int m_green[32];
};

#endif // AUDIOWAVE_H
