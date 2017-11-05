/*!
 * \file RenderFrameThread.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The render thread
 */

#include "RenderFrameThread.h"
#include <QDebug>

//Constructor
RenderFrameThread::RenderFrameThread()
{
    m_stop = false;
    m_initialized = false;
    m_renderFrame = false;
    m_frameReady = false;
}

//Destructor
RenderFrameThread::~RenderFrameThread()
{

}

//Init all objects
void RenderFrameThread::init(mlvObject_t *pMlvObject, uint8_t *pRawImage)
{
    m_mutex.lock();
    m_frameReady = false;
    m_pMlvObject = pMlvObject;
    m_pRawImage = pRawImage;
    m_mutex.unlock();
}

//Start rendering
void RenderFrameThread::renderFrame(uint32_t frameNumber)
{
    m_mutexRender.lock();
    m_frameNumber = frameNumber;
    m_renderFrame = true;
    m_frameReady = false;
    m_mutexRender.unlock();
}

//Is rendering finished?
bool RenderFrameThread::isFrameReady()
{
    return m_frameReady;
}

//Returns if there is a frame in the pipeline...
bool RenderFrameThread::isIdle()
{
    return !m_renderFrame;
}

//Stop the thread
void RenderFrameThread::stop()
{
    m_stop = true;
}

//Main loop of the thread
void RenderFrameThread::run(void)
{
    while( !m_stop )
    {
        m_mutexRender.lock();
        if( m_renderFrame )
        {
            drawFrame();
            m_renderFrame = false;
            m_frameReady = true;
        }
        m_mutexRender.unlock();
        msleep(1);
    }
    m_stop = false;
}

//render the picture
void RenderFrameThread::drawFrame()
{
    //Get frame from library
    m_mutex.lock();
    getMlvProcessedFrame8( m_pMlvObject, m_frameNumber, m_pRawImage );
    m_mutex.unlock();
    emit frameReady();
}
