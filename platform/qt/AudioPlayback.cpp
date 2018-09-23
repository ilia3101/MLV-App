/*!
 * \file AudioPlayback.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Handles the mlv audio playback
 */

#include "AudioPlayback.h"
#include <QDebug>

//Constructor
AudioPlayback::AudioPlayback( QObject *parent )
    : QObject( parent )
{
    m_audioEngineInitialized = false;
    m_audioEngineRunning = false;
}

//Destructor
AudioPlayback::~AudioPlayback()
{
    if( m_audioEngineInitialized ) resetAudioEngine();
}

//Initialize audio engine
void AudioPlayback::initAudioEngine( mlvObject_t *pMlvObject )
{
    if( !doesMlvHaveAudio( pMlvObject ) ) return;

    m_audioSampleRate = getMlvSampleRate( pMlvObject );
    m_audioChannels = getMlvAudioChannels( pMlvObject );
    m_pMlvAudioData = getMlvAudioData( pMlvObject );
    m_mlvAudioSize = getMlvAudioSize( pMlvObject );
    m_mlvFrameRate = getMlvFramerate( pMlvObject );

    if( m_audioEngineInitialized ) resetAudioEngine();

    //Set up the format, eg.
    QAudioFormat format;
    format.setSampleRate( m_audioSampleRate );
    format.setChannelCount( m_audioChannels );
    format.setSampleSize( 16 );
    format.setCodec( "audio/pcm" );
    format.setByteOrder( QAudioFormat::LittleEndian );
    format.setSampleType( QAudioFormat::SignedInt );
    m_pAudioOutput = new QAudioOutput( format, this );

    m_pByteArrayAudio = new QByteArray();
    m_pAudioStream = new QDataStream(m_pByteArrayAudio, QIODevice::ReadWrite);

    for( uint64_t x = 0; x < m_mlvAudioSize; x++ )
    {
        (*m_pAudioStream) << m_pMlvAudioData[x];
    }
    m_pAudioStream->device()->seek( 0 );
#ifdef Q_OS_LINUX
    m_pAudioOutput->setBufferSize( 131072 );
#elif defined(Q_OS_WIN)
    m_pAudioOutput->setBufferSize( 32768 );
#else //Q_OS_OSX
    m_pAudioOutput->setBufferSize( 524288 );
#endif
    m_pAudioOutput->setVolume( 1.0 );
    m_pAudioOutput->suspend();

    m_audioEngineInitialized = true;
}

//Unload the audio of a mlv
void AudioPlayback::resetAudioEngine()
{
    if( !m_audioEngineInitialized ) return;

    stop();
    delete m_pAudioStream;
    delete m_pByteArrayAudio;
    delete m_pAudioOutput;

    m_audioEngineInitialized = false;
    m_audioEngineRunning = false;
}

//Jump to frame
void AudioPlayback::jumpToPos( int frame )
{
    if( !m_audioEngineInitialized ) return;

    qint64 position = 4 * (qint64)( frame * m_audioSampleRate / m_mlvFrameRate );
    m_pAudioStream->device()->seek( position );
}

//Play audio
void AudioPlayback::play()
{
    if( !m_audioEngineInitialized ) return;

    m_pAudioOutput->start( m_pAudioStream->device() );
    m_pAudioOutput->resume();
    m_audioEngineRunning = true;
}

//Stop audio
void AudioPlayback::stop()
{
    if( !m_audioEngineInitialized ) return;

    if( !m_audioEngineRunning ) return;
    m_pAudioOutput->suspend();
    m_pAudioOutput->stop();
    m_pAudioOutput->reset();
    m_audioEngineRunning = false;
}

