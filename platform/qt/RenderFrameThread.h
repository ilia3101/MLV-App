/*!
 * \file RenderFrameThread.h
 * \author masc4ii
 * \copyright 2017
 * \brief The render thread
 */

#ifndef RENDERFRAMETHREAD_H
#define RENDERFRAMETHREAD_H

#include <QThread>
#include <QMutex>
#include "../../src/mlv_include.h"

class RenderFrameThread : public QThread
{
    Q_OBJECT

public:
    RenderFrameThread();
    ~RenderFrameThread();
    void init( mlvObject_t *pMlvObject,
          uint8_t *pRawImage );
    void renderFrame( uint32_t frameNumber );
    bool isFrameReady( void );
    bool isIdle( void );
    void stop( void );
    void lock( void ){ m_mutex.lock(); }
    void unlock( void ){ m_mutex.unlock(); }

signals:
    void frameReady( void );

private:
    QMutex m_mutex;
    mlvObject_t *m_pMlvObject;
    uint8_t *m_pRawImage;
    bool m_initialized;
    bool m_stop;
    bool m_renderFrame;
    bool m_frameReady;
    uint32_t m_frameNumber;

    void run( void );
    void drawFrame( void );
};

#endif // RENDERFRAMETHREAD_H
