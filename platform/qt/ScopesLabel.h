/*!
 * \file Curves.h
 * \author masc4ii
 * \copyright 2019
 * \brief Resizeable Scope Label
 */

#ifndef SCOPESLABEL_H
#define SCOPESLABEL_H

#include <QLabel>
#include <QResizeEvent>
#include <QTimer>
#include "Histogram.h"
#include "WaveFormMonitor.h"
#include "VectorScope.h"
#include "RenderFrameThread.h"

class ScopesLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ScopesLabel(QWidget *parent = nullptr);
    ~ScopesLabel();
    enum ScopeType{ ScopeHistogram, ScopeWaveForm, ScopeRgbParade, ScopeVectorScope, None };
    void setScope( RenderFrameThread *pRenderThread, uint16_t width, uint16_t height, ScopeType type );
    void setUnderOver( uint8_t underOver );

private:
    QImage m_imageScope;
    RenderFrameThread *m_pRenderThread = nullptr;
    uint16_t m_width;
    uint16_t m_height;
    uint8_t m_underOver = 0;
    ScopeType m_type = ScopeHistogram;
    QTimer *m_paintTimer;

protected:
    void resizeEvent(QResizeEvent *event);
    void paintScope( void );
    void drawLines( QPixmap &pic );
};

#endif // SCOPESLABEL_H
