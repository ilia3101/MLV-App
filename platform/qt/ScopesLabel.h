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
#include <Qt>
#include "Histogram.h"
#include "WaveFormMonitor.h"
#include "VectorScope.h"

class ScopesLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ScopesLabel(QWidget *parent = nullptr);
    ~ScopesLabel();
    enum ScopeType{ ScopeHistogram, ScopeWaveForm, ScopeRgbParade, ScopeVectorScope, None };
    void setScope(uint8_t *pPicture, uint16_t width, uint16_t height, bool under, bool over, ScopeType type );

private:
    QImage m_imageScope;
    uint16_t m_widthScope;
    uint16_t m_heightScope;
    QImage *m_pImageLabel;
    uint16_t m_widthLabel;
    Histogram *m_pHistogram;
    WaveFormMonitor *m_pWaveFormMonitor;
    VectorScope *m_pVectorScope;

protected:
    void resizeEvent(QResizeEvent* event);
    void paintScope( void );
};

#endif // SCOPESLABEL_H
