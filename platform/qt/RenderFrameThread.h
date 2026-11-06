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
#include <QWaitCondition>
#include "../../src/mlv_include.h"

class RenderFrameThread : public QThread
{
    Q_OBJECT

public:
    RenderFrameThread();
    ~RenderFrameThread();
    void init( mlvObject_t *pMlvObject );
    void renderFrame( uint32_t frameNumber );
    void waitUntilIdle();
    void stop();
    QMutex *mutex() { return &m_mutex; }

    // Caller must hold mutex()
    const uint8_t *currentFrame() { return m_rawImage.data(); }

signals:
    void frameReady();

private:
    QMutex m_mutex;
    QWaitCondition m_renderCondition;
    QWaitCondition m_idleCondition;
    mlvObject_t *m_pMlvObject = nullptr;
    std::vector<uint8_t> m_rawBuffer;
    std::vector<uint8_t> m_rawImage;
    bool m_stop = false;
    uint64_t m_requestID = 0;
    uint64_t m_lastProcessedID = 0;
    uint32_t m_frameNumber = 0;

    void run() override;
};

#endif // RENDERFRAMETHREAD_H
