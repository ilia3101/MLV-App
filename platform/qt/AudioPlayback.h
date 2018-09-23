/*!
 * \file AudioPlayback.h
 * \author masc4ii
 * \copyright 2017
 * \brief Handles the mlv audio playback
 */

#ifndef AUDIOPLAYBACK_H
#define AUDIOPLAYBACK_H

#include <QAudioOutput>
#include <QByteArray>
#include <QDataStream>
#include <Qt>
#include "../../src/mlv_include.h"

class AudioPlayback : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayback( QObject *parent = Q_NULLPTR );
    ~AudioPlayback();
    void initAudioEngine( mlvObject_t *pMlvObject );
    void resetAudioEngine( void );
    void jumpToPos( int frame );
    void play( void );
    void stop( void );

private:
    uint8_t *m_pMlvAudioData;
    uint64_t m_mlvAudioSize;
    uint32_t m_audioSampleRate;
    uint16_t m_audioChannels;

    bool m_audioEngineInitialized;
    bool m_audioEngineRunning;

    QByteArray *m_pByteArrayAudio;
    QDataStream *m_pAudioStream;
    QAudioOutput *m_pAudioOutput;

    double m_mlvFrameRate;
};

#endif // AUDIOPLAYBACK_H
