/*!
 * \file Curves.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Resizeable Scope Label
 */

#include "ScopesLabel.h"
#include <QPainter>
#include <QTimer>

#define RATIO 3

//Constructor
ScopesLabel::ScopesLabel( QWidget *parent )
    : QLabel(parent)
{
    m_paintTimer = new QTimer( this );
    m_paintTimer->setSingleShot( true );

    connect( m_paintTimer, &QTimer::timeout, this, &ScopesLabel::paintScope );
}

//Destructor
ScopesLabel::~ScopesLabel()
{
}

//Prepare and paint scope
void ScopesLabel::setScope( RenderFrameThread *pRenderThread, uint16_t width, uint16_t height, ScopeType type )
{
    m_pRenderThread = pRenderThread;
    m_width = width;
    m_height = height;
    m_type = type;

    if( m_pRenderThread )
    {
        // More efficient during playback
        m_paintTimer->start( 0 );
    }
    else
    {
        paintScope();
    }
}

void ScopesLabel::setUnderOver( uint8_t underOver )
{
    m_underOver = underOver;
}

//Resize event for scaling the scope label
void ScopesLabel::resizeEvent( QResizeEvent *event )
{
    QLabel::resizeEvent( event );

    int labelWidth = this->width() * devicePixelRatio();

    // Quickly resize
    QPixmap pic = QPixmap::fromImage( m_imageScope.scaled( labelWidth, labelWidth / RATIO, Qt::IgnoreAspectRatio, Qt::SmoothTransformation ) );
    pic.setDevicePixelRatio( devicePixelRatio() );

    drawLines( pic );
    setPixmap( pic );

    setMinimumWidth( 1 ); //Otherwise window won't be smaller than picture

    // Debounce (repaint later)
    m_paintTimer->start( 50 );
}

//Bring scope resized to label
void ScopesLabel::paintScope()
{
    int labelWidth = this->width() * devicePixelRatio();

    QMutexLocker locker( m_pRenderThread ? m_pRenderThread->mutex() : nullptr );

    if( !m_pRenderThread )
    {
        m_imageScope = QImage(labelWidth, labelWidth / RATIO, QImage::Format_RGB32);
        m_imageScope.fill( Qt::black );
    }
    else if( m_type == ScopeHistogram )
    {
        bool under = false;
        bool over = false;

        if( ( m_underOver & 0x01 ) == 0x01 ) under = true;
        if( ( m_underOver & 0x02 ) == 0x02 ) over = true;

        m_imageScope = Histogram::getHistogramFromRaw( m_pRenderThread->currentFrame(), m_width, m_height, labelWidth, labelWidth / RATIO, under, over );
    }
    else if( m_type == ScopeWaveForm )
    {
        m_imageScope = WaveFormMonitor::getWaveFormMonitorFromRaw( m_pRenderThread->currentFrame(), m_width, m_height, labelWidth, labelWidth / RATIO );
    }
    else if( m_type == ScopeRgbParade )
    {
        m_imageScope = WaveFormMonitor::getWaveFormMonitorFromRaw( m_pRenderThread->currentFrame(), m_width, m_height, labelWidth, labelWidth / RATIO, true );
    }
    else
    {
        VectorScope vectorScope = VectorScope( labelWidth, labelWidth / RATIO );
        m_imageScope = vectorScope.getVectorScopeFromRaw( m_pRenderThread->currentFrame(), m_width, m_height );
    }

    QPixmap pic = QPixmap::fromImage( m_imageScope );
    pic.setDevicePixelRatio( devicePixelRatio() );

    drawLines( pic );
    setPixmap( pic );

    setMinimumWidth( 1 ); //Otherwise window won't be smaller than picture
}

//Draw the grid lines onto the scopes
void ScopesLabel::drawLines( QPixmap &pic )
{
    QPainter painter( &pic );

    QPen pen;
    pen.setStyle( Qt::DotLine );
    pen.setWidth( 1 );
    pen.setBrush( QColor( 160, 160, 160, 128 ) );
    painter.setPen( pen );

    if( m_type == ScopeHistogram )
    {
        painter.drawLine( this->width()*0.1,  0, this->width()*0.1, this->height()-1 );
        painter.drawLine( this->width()*0.25, 0, this->width()*0.25, this->height()-1 );
        painter.drawLine( this->width()*0.5,  0, this->width()*0.5, this->height()-1 );
        painter.drawLine( this->width()*0.75, 0, this->width()*0.75, this->height()-1 );
        painter.drawLine( this->width()*0.9,  0, this->width()*0.9, this->height()-1 );
    }
    else if( m_type == ScopeWaveForm || m_type == ScopeRgbParade )
    {
        painter.drawLine( 0, this->height()*0.1,  this->width(), this->height()*0.1 );
        painter.drawLine( 0, this->height()*0.25, this->width(), this->height()*0.25 );
        painter.drawLine( 0, this->height()*0.5,  this->width(), this->height()*0.5 );
        painter.drawLine( 0, this->height()*0.75, this->width(), this->height()*0.75 );
        painter.drawLine( 0, this->height()*0.9,  this->width(), this->height()*0.9 );
    }
    else
    {
        int middleX = this->width() / 2;
        int middleY = this->height() / 2;

        int x = middleX - middleY;

        pen.setStyle( Qt::SolidLine );
        pen.setCapStyle( Qt::FlatCap );
        painter.setPen( pen );

        painter.drawLine( middleX + 1, 2, middleX + 1, this->height() - 2 );
        painter.drawLine( x + 3, middleY - 1,  x + this->height() - 1, middleY - 1 );

        pen.setWidth( 2 );
        painter.setPen( pen );
        painter.setRenderHint( QPainter::Antialiasing );

        painter.drawEllipse( x + 2, 1, this->height() - 2, this->height() - 2 );
    }
}
