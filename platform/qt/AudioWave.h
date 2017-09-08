#ifndef AUDIOWAVE_H
#define AUDIOWAVE_H

#include <QImage>

class AudioWave
{
public:
    AudioWave();
    ~AudioWave();
    QImage getMonoWave( uint16_t *pAudioTrack, uint64_t audioSize, uint16_t width );

private:
    QImage *m_pAudioWave;
};

#endif // AUDIOWAVE_H
