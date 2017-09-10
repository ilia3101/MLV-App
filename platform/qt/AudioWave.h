#ifndef AUDIOWAVE_H
#define AUDIOWAVE_H

#include <QImage>

class AudioWave
{
public:
    AudioWave();
    ~AudioWave();
    QImage getMonoWave(int16_t *pAudioTrack, uint64_t audioSize, uint16_t width );

private:
    QImage *m_pAudioWave;
    int m_red[32];
    int m_green[32];
};

#endif // AUDIOWAVE_H
