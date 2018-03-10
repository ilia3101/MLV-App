/*!
 * \file AudioPlayback.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Handles the mlv audio playback
 */

#include "AudioPlayback.h"
#include <QDebug>

//Constructor
AudioPlayback::AudioPlayback(mlvObject_t *pMlvObject , QObject *parent)
    : QObject( parent )
{
    m_pMlvObject = pMlvObject;
    m_audio_size = 0;
    m_pAudioData = NULL;
    m_audioLoaded = false;
    m_audioRunning = false;
}

//Destructor
AudioPlayback::~AudioPlayback()
{
    if( m_audioLoaded ) unloadAudio();
}

//Load the audio of a mlv
void AudioPlayback::loadAudio( mlvObject_t *pMlvObject )
{
    //Unload last file
    if( m_audioLoaded ) unloadAudio();

    //Add new pointer
    m_pMlvObject = pMlvObject;

    //No audio? Quit!
    if( !doesMlvHaveAudio( m_pMlvObject ) ) return;

    //Set up the format, eg.
    QAudioFormat format;
    format.setSampleRate( getMlvSampleRate( m_pMlvObject ) );
    format.setChannelCount( getMlvAudioChannels( m_pMlvObject ) );
    format.setSampleSize( 16 );
    format.setCodec( "audio/pcm" );
    format.setByteOrder( QAudioFormat::LittleEndian );
    format.setSampleType( QAudioFormat::SignedInt );
    m_pAudioOutput = new QAudioOutput( format, this );

    m_pByteArrayAudio = new QByteArray();
    m_pAudioStream = new QDataStream(m_pByteArrayAudio, QIODevice::ReadWrite);
    m_pAudioData = (uint8_t*)getMlvAudioData( m_pMlvObject, &m_audio_size );

    for( uint64_t x = 0; x < m_audio_size; x++ )
    {
        (*m_pAudioStream) << m_pAudioData[x];
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

    m_audioLoaded = true;
}

//Unload the audio of a mlv
void AudioPlayback::unloadAudio()
{
    if( !m_audioLoaded ) return;

    stop();
    if(m_pAudioData) free( m_pAudioData );
    m_pAudioData = NULL;
    delete m_pAudioStream;
    delete m_pByteArrayAudio;
    delete m_pAudioOutput;

    m_audioLoaded = false;
}

//Jump to frame
void AudioPlayback::jumpToPos( int frame )
{
    if( !doesMlvHaveAudio( m_pMlvObject ) ) return;

    qint64 position = 4 * (qint64)( frame * getMlvSampleRate( m_pMlvObject ) / getMlvFramerate( m_pMlvObject ) );
    m_pAudioStream->device()->seek( position );
}

//Play audio
void AudioPlayback::play()
{
    if( !doesMlvHaveAudio( m_pMlvObject ) ) return;

    m_pAudioOutput->start( m_pAudioStream->device() );
    m_pAudioOutput->resume();
    m_audioRunning = true;
}

//Stop audio
void AudioPlayback::stop()
{
    if( !doesMlvHaveAudio( m_pMlvObject ) ) return;

    if( !m_audioRunning ) return;
    m_pAudioOutput->suspend();
    m_pAudioOutput->stop();
    m_pAudioOutput->reset();
    m_audioRunning = false;
}

uint8_t* AudioPlayback::getAudioData()
{
    return m_pAudioData;
}

uint64_t AudioPlayback::getAudioSize()
{
    return m_audio_size;
}
