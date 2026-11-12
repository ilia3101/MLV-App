/*!
 * \file RenderFrameThread.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The render thread
 */

#include "RenderFrameThread.h"
#include <QDebug>

RenderFrameThread::RenderFrameThread()
{
}

RenderFrameThread::~RenderFrameThread()
{
    stop();
    wait();
}

void RenderFrameThread::init(mlvObject_t *pMlvObject)
{
    QMutexLocker locker( &m_mutex );

    m_pMlvObject = pMlvObject;

    size_t imageSize = (size_t)getMlvWidth( m_pMlvObject ) * (size_t)getMlvHeight( m_pMlvObject ) * 3;

    m_rawBuffer.resize( imageSize );
    m_rawImage.resize( imageSize );
}

void RenderFrameThread::renderFrame(uint32_t frameNumber)
{
    {
        QMutexLocker locker( &m_mutex );

        if( m_stop ) return;

        m_frameNumber = frameNumber;
        m_requestID++;
    }

    m_renderCondition.wakeOne();
}

void RenderFrameThread::waitUntilIdle()
{
    QMutexLocker locker( &m_mutex );
    while( m_requestID != m_lastProcessedID && !m_stop ) m_idleCondition.wait( &m_mutex );
}

void RenderFrameThread::stop()
{
    {
        QMutexLocker locker( &m_mutex );
        m_stop = true;
    }

    m_renderCondition.wakeAll();
    m_idleCondition.wakeAll();
}

void RenderFrameThread::run()
{
    const int threadCount = QThread::idealThreadCount();

    while( true )
    {
        uint64_t requestID;
        uint32_t frameNumber;

        {
            QMutexLocker locker( &m_mutex );

            while( m_requestID == m_lastProcessedID && !m_stop ) m_renderCondition.wait( &m_mutex );

            if( m_stop ) break;

            requestID = m_requestID;
            frameNumber = m_frameNumber;
        }

        getMlvProcessedFrame8( m_pMlvObject, frameNumber, m_rawBuffer.data(), threadCount );

        {
            QMutexLocker locker( &m_mutex );

            if( requestID != m_requestID )
            {
                m_idleCondition.wakeAll();
                continue;
            }

            m_rawImage.swap( m_rawBuffer );

            m_lastProcessedID = requestID;
            m_idleCondition.wakeAll();
        }

        emit frameReady();
    }
}
