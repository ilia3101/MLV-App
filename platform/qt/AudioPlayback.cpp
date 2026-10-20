/*!
 * \file AudioPlayback.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Handles the mlv audio playback
 */

#include "AudioPlayback.h"
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioFormat>
#endif
#include <QMediaDevices>
#include <QDebug>

//Constructor
AudioPlayback::AudioPlayback( QObject *parent )
    : QObject( parent )
{
    m_audioEngineInitialized = false;
    m_audioEngineRunning = false;

    QMediaDevices *mediaDevices = new QMediaDevices( this );
    connect( mediaDevices, &QMediaDevices::audioOutputsChanged, this, &AudioPlayback::outputDeviceChanged );
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
    m_audioFormat.setSampleRate( m_audioSampleRate );
    m_audioFormat.setChannelCount( m_audioChannels );
 #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_audioFormat.setSampleSize( 16 );
    m_audioFormat.setCodec( "audio/pcm" );
    m_audioFormat.setByteOrder( QAudioFormat::LittleEndian );
    m_audioFormat.setSampleType( QAudioFormat::SignedInt );
    m_pAudioOutput = new QAudioOutput( m_audioFormat, this );
#else
    m_audioFormat.setSampleFormat( QAudioFormat::Int16 );
    m_pAudioOutput = new QAudioSink( m_audioFormat, this );
#endif

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
#else //Q_OS_MACOS
    m_pAudioOutput->setBufferSize( 524288 );
#endif
    m_pAudioOutput->setVolume( 1.0 );
    m_pAudioOutput->suspend();

    m_audioEngineInitialized = true;
}

void AudioPlayback::outputDeviceChanged()
{
    if( !m_audioEngineInitialized ) return;

    bool wasPlaying = m_audioEngineRunning;
    qint64 position = m_pAudioStream->device()->pos();

    stop();

    delete m_pAudioOutput;
    m_pAudioOutput = new QAudioSink( m_audioFormat, this );

    m_pAudioStream->device()->seek( position );
#ifdef Q_OS_LINUX
    m_pAudioOutput->setBufferSize( 131072 );
#elif defined(Q_OS_WIN)
    m_pAudioOutput->setBufferSize( 32768 );
#else //Q_OS_MACOS
    m_pAudioOutput->setBufferSize( 524288 );
#endif
    m_pAudioOutput->setVolume( 1.0 );

    if( wasPlaying ) play();
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

