/*!
 * \file HueVsDiagram.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief The gradation HueVsSat element
 */

#include "HueVsDiagram.h"
#include <QPainter>
#include <QDebug>
#include <QMouseEvent>
#include <cmath>
#include "../../src/processing/interpolation/cosine_interpolation.h"

#define SIZEW           this->width()
#define SIZEH           m_height
#define MAXHEIGHT       233
#define HALFSIZEW       (SIZEW / 2.0)
#define HALFSIZEH       (SIZEH / 2.0)

#define POINT_RADIUS    4.0
#define GRAB_RADIUS     (POINT_RADIUS + 2.0)

//Constructor
HueVsDiagram::HueVsDiagram(QWidget *parent)
    : QLabel(parent) {
    m_height = SIZEW;
    m_cursor = QPoint( 0, 0 );
    m_pointSelected = false;
    m_pFrameChanged = NULL;
    m_pProcessing = NULL;
    m_LoadAll = false;
    m_diagramType = HueVsLuminance;
    resetLine();
}

//Destructor
HueVsDiagram::~HueVsDiagram()
{
}

//Set processing object to this class
void HueVsDiagram::setProcessingObject(processingObject_t *processing)
{
    m_pProcessing = processing;
}

//Set the flag for rendering the viewers picture
void HueVsDiagram::setFrameChangedPointer(bool *pFrameChanged)
{
    m_pFrameChanged = pFrameChanged;
}

//Draw the HueVsSat object on a label
void HueVsDiagram::paintElement(bool frameChanged)
{
    QImage image = QImage( SIZEW * devicePixelRatio(), SIZEH * devicePixelRatio(), QImage::Format_RGB32 );
    image.setDevicePixelRatio( devicePixelRatio() );

    image.fill( QColor( 0, 0, 0, 255 ) );
    QPainter painterTc( &image );
    painterTc.setPen( Qt::NoPen );

    QLinearGradient gradient1;

    //HueVsHue
    if( m_diagramType == HueVsHue )
    {
        double dx = SIZEH;
        double dy = -SIZEW / 3.0;

        // normalize direction
        double len = std::sqrt( dx * dx + dy * dy );
        dx /= len;
        dy /= len;

        QPointF dir( dx, dy );
        QPointF center( HALFSIZEW, HALFSIZEH );

        double halfLength = std::abs( dx ) * HALFSIZEW;

        QPointF start = center - dir * halfLength;
        QPointF end   = center + dir * halfLength;

        gradient1 = QLinearGradient( start, end );
    }
    else
    {
        gradient1 = QLinearGradient( 0, 0, SIZEW, 0 );
    }

    gradient1.setColorAt( 0, QColor( 255, 0, 0, 255 ) );
    gradient1.setColorAt( 0.167, QColor( 255, 255, 0, 255 ) );
    gradient1.setColorAt( 0.333, QColor( 0, 255, 0, 255 ) );
    gradient1.setColorAt( 0.5, QColor( 0, 255, 255, 255 ) );
    gradient1.setColorAt( 0.667, QColor( 0, 0, 255, 255 ) );
    gradient1.setColorAt( 0.833, QColor( 255, 0, 255, 255 ) );
    gradient1.setColorAt( 1, QColor( 255, 0, 0, 255 ) );
    painterTc.setBrush( gradient1 );
    painterTc.drawRect( 0, 0, SIZEW, SIZEH );

    //HueVsSat
    if( m_diagramType == HueVsSaturation )
    {
        QLinearGradient gradient2( 0, 0, 0, SIZEH );
        gradient2.setColorAt( 0, QColor( 128, 128, 128, 0 ) );
        gradient2.setColorAt( 1, QColor( 128, 128, 128, 255 ) );
        painterTc.setBrush( gradient2 );
        painterTc.drawRect( 0, 0, SIZEW, SIZEH );
    }
    //HueVsLuminance
    else if( m_diagramType == HueVsLuminance )
    {
        QLinearGradient gradient2( 0, 0, 0, SIZEH );
        gradient2.setColorAt( 0, QColor( 255, 255, 255, 255 ) );
        gradient2.setColorAt( 0.5, QColor( 128, 128, 128, 0 ) );
        gradient2.setColorAt( 1, QColor( 0, 0, 0, 255 ) );
        painterTc.setBrush( gradient2 );
        painterTc.drawRect( 0, 0, SIZEW, SIZEH );
    }
    //LuminanceVsSat
    else if( m_diagramType == LuminanceVsSaturation )
    {
        QLinearGradient gradient2( SIZEW, 0, 0, 0 );
        gradient2.setColorAt( 0, QColor( 255, 255, 255, 255 ) );
        gradient2.setColorAt( 1, QColor( 0, 0, 0, 255 ) );
        painterTc.setBrush( gradient2 );
        painterTc.drawRect( 0, 0, SIZEW, SIZEH );

        QLinearGradient gradient3( 0, 0, 0, SIZEH );
        gradient3.setColorAt( 0.5, QColor( 128, 128, 128, 0 ) );
        gradient3.setColorAt( 1.0, QColor( 128, 128, 128, 255 ) );
        painterTc.setBrush( gradient3 );
        painterTc.drawRect( 0, 0, SIZEW, SIZEH );
    }

    //Lines
    painterTc.setPen( QColor( 128, 128, 128, 128 ) );
    painterTc.save();
    for( int i = 1; i < 3; i++ )
    {
        painterTc.setCompositionMode( QPainter::CompositionMode_Multiply );
        painterTc.drawLine( QPointF( (i*SIZEW/3+0.5)-1, 0 ), QPointF( (i*SIZEW/3+0.5)-1, SIZEH ) );
        painterTc.setCompositionMode( QPainter::CompositionMode_Screen );
        painterTc.drawLine( QPointF( (i*SIZEW/3+0.5)  , 0 ), QPointF( (i*SIZEW/3+0.5)  , SIZEH ) );
    }

    for( int i = 1; i < 4; i++ )
    {
        painterTc.setCompositionMode( QPainter::CompositionMode_Multiply );
        painterTc.drawLine( QPointF( 0, (i*SIZEH/4+0.5)-1 ), QPointF( SIZEW, (i*SIZEH/4+0.5)-1 ) );
        painterTc.setCompositionMode( QPainter::CompositionMode_Screen );
        painterTc.drawLine( QPointF( 0, (i*SIZEH/4+0.5) )  , QPointF( SIZEW, (i*SIZEH/4+0.5) )   );
    }

    painterTc.restore();
    painterTc.setRenderHint( QPainter::Antialiasing );
    paintLine( m_whiteLine, &painterTc, QColor(255,255,255,255), true );

    //Paint to label
    QPixmap pic = QPixmap::fromImage( image );
    setPixmap( pic );

    setMinimumWidth( 1 ); //Otherwise window won't be smaller than picture

    if( frameChanged && m_pFrameChanged != NULL ) *m_pFrameChanged = true;
}

//Reset the lines
void HueVsDiagram::resetLine()
{
    initLine( &m_whiteLine );
}

//Default color points
void HueVsDiagram::resetLineDefaultPoints()
{
    QVector<QPointF> *line = &m_whiteLine;
    initLine(line);
    line->insert( 1, QPointF( 1.0/6, 0.0 ) );
    line->insert( 2, QPointF( 1.0/6*2, 0.0 ) );
    line->insert( 3, QPointF( 1.0/6*3, 0.0 ) );
    line->insert( 4, QPointF( 1.0/6*4, 0.0 ) );
    line->insert( 5, QPointF( 1.0/6*5, 0.0 ) );
}

//Load configuration string to settings
void HueVsDiagram::setConfiguration(QString config)
{
    if( config.size() <= 0 ) return;
    QVector<QPointF> *line;
    line = &m_whiteLine;

    line->clear();
    while( config.size() > 0 && !config.startsWith( "?" ) )
    {
        QString val = config.left( config.indexOf( ";" ) );
        config.remove( 0, config.indexOf( ";" )+1 );
        float valueX = val.toFloat();
        val = config.left( config.indexOf( ";" ) );
        config.remove( 0, config.indexOf( ";" )+1 );
        float valueY = val.toFloat();
        line->append( QPointF( valueX, valueY ) );
    }

    m_LoadAll = true;
    paintElement();
    m_LoadAll = false;
}

//Get a configuration string to save settings
QString HueVsDiagram::configuration()
{
    QString config;
    config.clear();

    for( int i = 0; i < m_whiteLine.size(); i++ )
    {
        config.append( QString( "%1;%2;" ).arg( m_whiteLine.at(i).x() ).arg( m_whiteLine.at(i).y() ) );
    }

    return config;
}

//Change diagram type
void HueVsDiagram::setDiagramType(HueVsDiagram::DiagramType type)
{
    m_diagramType = type;
}

//Paint line
void HueVsDiagram::paintLine(QVector<QPointF> line, QPainter *pPainter, QColor color, bool active)
{
    //Make color a bit more grey, if line is not active
    if( !active )
    {
        color = QColor( color.red()/2.0, color.green()/2.0, color.blue()/2.0, color.alpha() );
    }

    //Build input sets
    float *pXin = (float*)malloc( sizeof(float) * line.size() );
    float *pYin = (float*)malloc( sizeof(float) * line.size() );
    //Data into input sets
    for( int i = 0; i < line.size(); i++ )
    {
        pXin[i] = line.at(i).x();
        pYin[i] = line.at(i).y();
    }
    //Build output sets
    float *pXout = (float*)malloc( sizeof(float) * SIZEW );
    float *pYout = (float*)malloc( sizeof(float) * SIZEW );

    int numIn = line.size();
    int numOut = SIZEW;

    //Data into x of output sets
    for( int i = 0; i < numOut; i++ )
    {
        pXout[i] = i / (float)SIZEW;
    }

    //Get the interpolated line
    int ret = cosine_interpolate( pXin , pYin , &numIn, pXout, pYout, &numOut );
    //qDebug() << ret;

    float subpx = SIZEH % 2 ? 0 : 0.5;

    //Draw the line
    if( ret == 0 )
    {
        pPainter->setPen( QPen( QBrush( color ), 1.01 ) );
        if( pYout[0] > 1.0 ) pYout[0] = 1.0;
        else if( pYout[0] < -1.0 ) pYout[0] = -1.0;
        for( int i = 1; i < numOut; i++ )
        {
            if( pYout[i] > 1.0 ) pYout[i] = 1.0;
            else if( pYout[i] < -1.0 ) pYout[i] = -1.0;
            pPainter->drawLine( QPointF( pXout[i-1]*SIZEW, SIZEH*(0.5-pYout[i-1])+subpx ), QPointF( pXout[i]*SIZEW, SIZEH*(0.5-pYout[i])+subpx ) );
        }
    }

    //Draw Points
    if( active )
    {
        pPainter->setPen( QPen( QBrush( QColor( 0, 0, 0, 96 ) ), 1.01 ) );
        pPainter->setBrush( color );
        for( int i = 0; i < line.size(); i++ )
        {
            pPainter->drawEllipse( QPointF( pXin[i]*SIZEW, SIZEH*(0.5-pYin[i])+subpx ), POINT_RADIUS, POINT_RADIUS );
        }
    }

    //Set Procesing Element if available
    if( active || m_LoadAll )
    {
        if( m_pProcessing != NULL )
        {
            if( m_diagramType == HueVsHue )
            {
                processingSetHueVsCurves( m_pProcessing, numIn, pXin, pYin, 0 );
            }
            else if( m_diagramType == HueVsSaturation )
            {
                processingSetHueVsCurves( m_pProcessing, numIn, pXin, pYin, 1 );
            }
            else if( m_diagramType == HueVsLuminance )
            {
                processingSetHueVsCurves( m_pProcessing, numIn, pXin, pYin, 2 );
            }
            else if( m_diagramType == LuminanceVsSaturation )
            {
                processingSetHueVsCurves( m_pProcessing, numIn, pXin, pYin, 3 );
            }
        }
    }

    //CleanUp
    free( pXin );
    free( pYin );
    free( pXout );
    free( pYout );
}

//Init / delete line
void HueVsDiagram::initLine(QVector<QPointF> *line)
{
    line->clear();
    line->append( QPointF( 0.0, 0.0 ) );
    line->append( QPointF( 1.0, 0.0 ) );
}

//Mouse pressed -> select a point
void HueVsDiagram::mousePressEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = &m_whiteLine;

    QPointF mousePos = mouse->localPos();

    double mx = mousePos.x();
    double my = HALFSIZEH - mousePos.y();

    if( std::abs( mx - ( line->at( 0 ).x() * SIZEW ) ) <= GRAB_RADIUS
        && std::abs( my - ( line->at( 0 ).y() * SIZEH ) ) <= GRAB_RADIUS )
    {
        m_firstPoint = true;
    }
    else if( std::abs( mx - ( line->at( line->count() - 1 ).x() * SIZEW ) ) <= GRAB_RADIUS
               && std::abs( my - ( line->at( line->count() - 1 ).y() * SIZEH ) ) <= GRAB_RADIUS )
    {
        m_lastPoint = true;
    }
    else
    {
        for( int i = 1; i < line->count()-1; i++ )
        {
            if( std::abs( mx - ( line->at( i ).x() * SIZEW ) ) <= GRAB_RADIUS
                && std::abs( my - ( line->at( i ).y() * SIZEH ) ) <= GRAB_RADIUS )
            {
                line->removeAt( i );
                break;
            }
        }
        //New point
        m_pointSelected = true;
    }
}

//Mouse doubleclicked -> reset/delete point
void HueVsDiagram::mouseDoubleClickEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = &m_whiteLine;

    QPointF mousePos = mouse->localPos();

    double mx = mousePos.x();
    double my = HALFSIZEH - mousePos.y();

    bool firstPoint = std::abs( mx - ( line->at( 0 ).x() * SIZEW ) ) <= GRAB_RADIUS
                      && std::abs( my - ( line->at( 0 ).y() * SIZEH ) ) <= GRAB_RADIUS;

    bool lastPoint = std::abs( mx - ( line->at( line->count() - 1 ).x() * SIZEW ) ) <= GRAB_RADIUS
                     && std::abs( my - ( line->at( line->count() - 1 ).y() * SIZEH ) ) <= GRAB_RADIUS;

    if( firstPoint || lastPoint )
    {
        if( firstPoint || m_diagramType != LuminanceVsSaturation )
        {
            QPointF point = line->takeFirst();
            point.setY( 0 );
            line->prepend( point );
        }

        if( lastPoint || m_diagramType != LuminanceVsSaturation )
        {
            QPointF point = line->takeLast();
            point.setY( 0 );
            line->append( point );
        }

        paintElement();
        return;
    }

    for( int i = 1; i < line->count()-1; i++ )
    {
        if( std::abs( mx - ( line->at( i ).x() * SIZEW ) ) <= GRAB_RADIUS
            && std::abs( my - ( line->at( i ).y() * SIZEH ) ) <= GRAB_RADIUS )
        {
            line->removeAt( i );
            paintElement();
            break;
        }
    }
}

//Mouse released -> deselect point
void HueVsDiagram::mouseReleaseEvent(QMouseEvent *mouse)
{
    movePoint( mouse->localPos().x(), mouse->localPos().y(), false );
    m_pointSelected = false;
    m_firstPoint = false;
    m_lastPoint = false;
}

//Mouse moved -> move marker
void HueVsDiagram::mouseMoveEvent(QMouseEvent *mouse)
{
    movePoint( mouse->localPos().x(), mouse->localPos().y(), true );
}

//Move a point around
void HueVsDiagram::movePoint(qreal x, qreal y, bool release)
{
    QVector<QPointF> *line = &m_whiteLine;
    //move the marker
    if( m_pointSelected )
    {
        if ( 0.0001 > x / (float)SIZEW ) x = 0.0001 * (float)SIZEW;
        if ( 0.9999 < x / (float)SIZEW ) x = 0.9999 * (float)SIZEW;
        if ( 0.0001 > 1.0-(y / (float)SIZEH ) ) y = SIZEH;
        if ( 1.0 < 1.0-(y / (float)SIZEH ) ) y = 0.0;

        int i;
        //Search the right position for the grabbed point
        for( i = 0; i < line->count()-1; i++ )
        {
            if( line->at(i).x() > x / (float)SIZEW )
            {
                break;
            }
        }
        //insert it for drawing
        line->insert( i, QPointF( x / (float)SIZEW,
                                  1.0-((y + HALFSIZEH) / (float)SIZEH )) );
        paintElement();
        //and grab it again
        if( release ) line->removeAt( i );
    }
    //Drag first & last point up & down
    else if( ( m_firstPoint || m_lastPoint ) && ( m_diagramType != LuminanceVsSaturation ) )
    {
        float yNew = 1.0-((y + HALFSIZEH) / (float)SIZEH );
        QPointF point = line->takeFirst();
        if( yNew > 0.5 ) yNew = 0.5;
        else if( yNew < -0.5 ) yNew = -0.5;
        point.setY( yNew );
        line->prepend( point );
        point = line->takeLast();
        point.setY( yNew );
        line->append( point );
        paintElement();
    }
    else if( m_diagramType == LuminanceVsSaturation )
    {
        float yNew = 1.0-((y + HALFSIZEH) / (float)SIZEH );
        if( m_firstPoint )
        {
            QPointF point = line->takeFirst();
            if( yNew > 0.5 ) yNew = 0.5;
            else if( yNew < -0.5 ) yNew = -0.5;
            point.setY( yNew );
            line->prepend( point );
        }
        if( m_lastPoint )
        {
            QPointF point = line->takeLast();
            if( yNew > 0.5 ) yNew = 0.5;
            else if( yNew < -0.5 ) yNew = -0.5;
            point.setY( yNew );
            line->append( point );
        }
        paintElement();
    }
}

//Resize the diagram
void HueVsDiagram::resizeEvent(QResizeEvent *event)
{
    m_height = SIZEW > MAXHEIGHT ? MAXHEIGHT : SIZEW;
    paintElement( false );
    event->accept();
}
