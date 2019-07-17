/*!
 * \file Curves.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Resizeable Scope Label
 */

#include "ScopesLabel.h"

#define SIZEW                (m_widthLabel * devicePixelRatio())
#define SIZEH                (SIZEW * 160 / 511)
#define HALFSIZEW            (SIZEW/2)
#define HALFSIZEH            (SIZEH/2)

//Constructor
ScopesLabel::ScopesLabel(QWidget *parent)
    : QLabel(parent)
{
    m_pHistogram = new Histogram();
    m_pVectorScope = new VectorScope( 511, 160 );
    m_pWaveFormMonitor = new WaveFormMonitor( 200 );
    m_widthLabel = size().width();
    m_imageScope = QImage(1, 1, QImage::Format_RGB888);
    m_imageScope.fill( Qt::black );
    m_pImageLabel = new QImage( SIZEW, SIZEH, QImage::Format_RGBA8888 );
    paintScope();
}

//Destructor
ScopesLabel::~ScopesLabel()
{
    delete m_pVectorScope;
    delete m_pWaveFormMonitor;
    delete m_pHistogram;
    delete m_pImageLabel;
}

//Prepare and paint scope
void ScopesLabel::setScope(uint8_t *pPicture, uint16_t width, uint16_t height, bool under, bool over, ScopeType type)
{
    static int w = -1;
    static int h = -1;

    if( w != width || h != height )
    {
        delete m_pWaveFormMonitor;
        m_pWaveFormMonitor = new WaveFormMonitor( width );

        w = width;
        h = height;
    }

    if( type == ScopeType::None )
    {
        m_imageScope.fill( QColor( 0, 0, 0, 255 ) );
    }
    else if( type == ScopeType::ScopeHistogram )
    {
        m_imageScope = m_pHistogram->getHistogramFromRaw( pPicture, width, height, under, over );
    }
    else if( type == ScopeType::ScopeWaveForm )
    {
        m_imageScope = m_pWaveFormMonitor->getWaveFormMonitorFromRaw( pPicture, width, height );
    }
    else if( type == ScopeType::ScopeRgbParade )
    {
        m_imageScope = m_pWaveFormMonitor->getParadeFromRaw( pPicture, width, height );
    }
    else
    {
        m_imageScope = m_pVectorScope->getVectorScopeFromRaw( pPicture, width, height );
    }
    paintScope();
}

//Resize event for scaling the scope label
void ScopesLabel::resizeEvent(QResizeEvent *event)
{
    m_widthLabel = size().width();
    paintScope();
    event->accept();
}

//Bring scope resized to label
void ScopesLabel::paintScope()
{
    //Resize
    delete m_pImageLabel;
    m_pImageLabel = new QImage( m_imageScope.scaled( SIZEW, SIZEH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation ) );

    //Paint to label
    QPixmap pic = QPixmap::fromImage( *m_pImageLabel );
    pic.setDevicePixelRatio( devicePixelRatio() );
    setPixmap( pic );

    //setMinimumSize( 1, 180 ); //Otherwise window won't be smaller than picture
    setMinimumWidth( 1 );
}
