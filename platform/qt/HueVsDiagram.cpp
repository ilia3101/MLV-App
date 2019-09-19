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
#include "../../src/processing/interpolation/cosine_interpolation.h"

#define SIZEW                (m_width * 2)
#define SIZEH                360
#define HALFSIZEW            (SIZEW/2)
#define HALFSIZEH            (SIZEH/2)
#define QUARTERSIZEW         (SIZEW/4)
#define QUARTERSIZEH         (SIZEH/4)
#define THREEQUARTERSIZEW    (SIZEW*3/4)
#define THREEQUARTERSIZEH    (SIZEH*3/4)

//Constructor
HueVsDiagram::HueVsDiagram(QWidget *parent)
    : QLabel(parent) {
    m_width = size().width();
    m_pImage = new QImage( SIZEW, SIZEH, QImage::Format_RGBA8888 );
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
    delete m_pImage;
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
void HueVsDiagram::paintElement()
{
    delete m_pImage;
    m_pImage = new QImage( SIZEW, SIZEH, QImage::Format_RGBA8888 );

    m_pImage->fill( QColor( 0, 0, 0, 255 ) );
    QPainter painterTc( m_pImage );
    painterTc.setRenderHint(QPainter::Antialiasing);

    //Color
    QLinearGradient gradient1( 0, 0, SIZEW, 0 );
    gradient1.setColorAt( 0, QColor( 255, 0, 0, 255 ) );
    gradient1.setColorAt( 0.18, QColor( 255, 255, 0, 255 ) );
    gradient1.setColorAt( 0.375, QColor( 0, 255, 0, 255 ) );
    gradient1.setColorAt( 0.5, QColor( 0, 255, 255, 255 ) );
    gradient1.setColorAt( 0.68, QColor( 0, 0, 255, 255 ) );
    gradient1.setColorAt( 0.875, QColor( 255, 0, 255, 255 ) );
    gradient1.setColorAt( 1, QColor( 255, 0, 0, 255 ) );
    painterTc.setBrush( gradient1 );
    painterTc.drawRect( 0, 0, SIZEW, SIZEH );

    //HueVsHue
    if( m_diagramType == HueVsHue )
    {
        for( int y = 0; y < SIZEH; y++ )
        {
            for( int x = 0; x < SIZEW; x++ )
            {
                if( y == HALFSIZEH ) continue;
                int getFrom = x - 60.0 / 360.0 * (y - HALFSIZEH);
                if( getFrom >= SIZEW ) getFrom -= SIZEW;
                if( getFrom < 0 ) getFrom += SIZEW;
                QColor color = m_pImage->pixelColor( getFrom, HALFSIZEH );
                if( getFrom < 2 || getFrom > SIZEW-2 ) color = QColor( 255, 0, 0, 255 );
                m_pImage->setPixelColor( x, y, color );
            }
        }
    }
    //HueVsSat
    else if( m_diagramType == HueVsSaturation )
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
        gradient2.setColorAt( 0.5, QColor( 128, 128, 128, 255 ) );
        gradient2.setColorAt( 1, QColor( 0, 0, 0, 255 ) );
        painterTc.setBrush( gradient2 );
        painterTc.drawRect( 0, 0, SIZEW, SIZEH );

        QLinearGradient gradient3( 0, 0, 0, SIZEH );
        gradient3.setColorAt( 0.0, QColor( 50, 50, 50, 0 ) );
        gradient3.setColorAt( 0.5, QColor( 50, 50, 50, 255 ) );
        gradient3.setColorAt( 1.0, QColor( 50, 50, 50, 0 ) );
        painterTc.setBrush( gradient3 );
        painterTc.drawRect( 0, 0, SIZEW, SIZEH );
    }

    //Lines
    painterTc.setPen( QColor( 100, 100, 100, 255 ) );
    for( int i = 1; i < 3; i++ )
    {
        painterTc.drawLine( (i*SIZEW/3)-1, 0, (i*SIZEW/3)-1, SIZEH );
        painterTc.drawLine( (i*SIZEW/3)  , 0, (i*SIZEW/3)  , SIZEH );
    }

    for( int i = 1; i < 4; i++ )
    {
        painterTc.drawLine( 0, (i*SIZEH/4)-1, SIZEW, (i*SIZEH/4)-1 );
        painterTc.drawLine( 0, (i*SIZEH/4)  , SIZEW, (i*SIZEH/4)   );
    }

    paintLine( m_whiteLine, &painterTc, QColor(255,255,255,255), true );

    //Paint to label
    QPixmap pic = QPixmap::fromImage( *m_pImage ).scaled( HALFSIZEW * devicePixelRatio(), (HALFSIZEH) * devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation );
    pic.setDevicePixelRatio( devicePixelRatio() );
    setPixmap( pic );

    setMinimumSize( 1, 180 ); //Otherwise window won't be smaller than picture
}

//Reset the lines
void HueVsDiagram::resetLine()
{
    initLine( &m_whiteLine );
}

//Load configuration string to settings
void HueVsDiagram::setConfiguration(QString config)
{
    if( config.count() <= 0 ) return;
    QVector<QPointF> *line;
    line = &m_whiteLine;

    line->clear();
    while( config.count() > 0 && !config.startsWith( "?" ) )
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

    for( int i = 0; i < m_whiteLine.count(); i++ )
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
        color = QColor( color.red()/2.0, color.green()/2.0, color.blue()/2, color.alpha() );
    }

    //Build input sets
    float *pXin = (float*)malloc( sizeof(float) * line.count() );
    float *pYin = (float*)malloc( sizeof(float) * line.count() );
    //Data into input sets
    for( int i = 0; i < line.count(); i++ )
    {
        pXin[i] = line.at(i).x();
        pYin[i] = line.at(i).y();
    }
    //Build output sets
    float *pXout = (float*)malloc( sizeof(float) * SIZEW );
    float *pYout = (float*)malloc( sizeof(float) * SIZEW );

    int numIn = line.count();
    int numOut = SIZEW;

    //Data into x of output sets
    for( int i = 0; i < numOut; i++ )
    {
        pXout[i] = i / (float)SIZEW;
    }

    //Get the interpolated line
    int ret = cosine_interpolate( pXin , pYin , &numIn, pXout, pYout, &numOut );
    //qDebug() << ret;

    //Draw the line
    if( ret == 0 )
    {
        pPainter->setPen( QPen( QBrush( color ), 2 ) );
        if( pYout[0] > 1.0 ) pYout[0] = 1.0;
        else if( pYout[0] < -1.0 ) pYout[0] = -1.0;
        for( int i = 1; i < numOut; i++ )
        {
            if( pYout[i] > 1.0 ) pYout[i] = 1.0;
            else if( pYout[i] < -1.0 ) pYout[i] = -1.0;
            pPainter->drawLine( pXout[i-1]*SIZEW, HALFSIZEH-(pYout[i-1]*SIZEH), pXout[i]*SIZEW, HALFSIZEH-(pYout[i]*SIZEH) );
        }
    }

    //Draw Points
    if( active )
    {
        pPainter->setPen( QPen( QBrush( color ), 1 ) );
        pPainter->setBrush( color );
        for( int i = 0; i < line.count(); i++ )
        {
            pPainter->drawEllipse( pXin[i]*SIZEW-7, HALFSIZEH-(pYin[i]*SIZEH)-7, 15, 15 );
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
        if( m_pFrameChanged != NULL ) *m_pFrameChanged = true;
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

    if( ( mouse->localPos().x() ) < line->at(0).x() / 2 * (float)SIZEW+10
     && ( mouse->localPos().x() ) > line->at(0).x() / 2 * (float)SIZEW-10
     && ( QUARTERSIZEH-mouse->localPos().y() ) < (line->at(0).y() / 2 * (float)SIZEH+10)
     && ( QUARTERSIZEH-mouse->localPos().y() ) > (line->at(0).y() / 2 * (float)SIZEH-10) )
    {
        m_firstPoint = true;
    }
    else if( ( mouse->localPos().x() ) < line->at(line->count()-1).x() / 2 * (float)SIZEW+10
     && ( mouse->localPos().x() ) > line->at(line->count()-1).x() / 2 * (float)SIZEW-10
     && ( QUARTERSIZEH-mouse->localPos().y() ) < (line->at(line->count()-1).y() / 2 * (float)SIZEH+10)
     && ( QUARTERSIZEH-mouse->localPos().y() ) > (line->at(line->count()-1).y() / 2 * (float)SIZEH-10) )
    {
        m_lastPoint = true;
    }
    else
    {
        for( int i = 1; i < line->count()-1; i++ )
        {
            /*qDebug() << mouse->localPos().x() << mouse->localPos().y()
                     << m_whiteLine.at(i).x() / 2 * (float)SIZE
                     << m_whiteLine.at(i).y() / 2 * (float)SIZE;*/
            if( ( mouse->localPos().x() ) < line->at(i).x() / 2 * (float)SIZEW+10
             && ( mouse->localPos().x() ) > line->at(i).x() / 2 * (float)SIZEW-10
             && ( QUARTERSIZEH-mouse->localPos().y() ) < (line->at(i).y() / 2 * (float)SIZEH+10)
             && ( QUARTERSIZEH-mouse->localPos().y() ) > (line->at(i).y() / 2 * (float)SIZEH-10) )
            {
                line->removeAt( i );
                break;
            }
        }
        //New point
        m_pointSelected = true;
    }
}

//Mouse doubleclicked -> delete point
void HueVsDiagram::mouseDoubleClickEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = &m_whiteLine;
    for( int i = 1; i < line->count()-1; i++ )
    {
        if( ( mouse->localPos().x() ) < line->at(i).x() / 2 * (float)SIZEW+10
         && ( mouse->localPos().x() ) > line->at(i).x() / 2 * (float)SIZEW-10
         && ( QUARTERSIZEH-mouse->localPos().y() ) < (line->at(i).y() / 2 * (float)SIZEH+10)
         && ( QUARTERSIZEH-mouse->localPos().y() ) > (line->at(i).y() / 2 * (float)SIZEH-10) )
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
        if ( 0.0001 > x * 2 / (float)SIZEW ) x = 0.001 / 2.0 * (float)SIZEW;
        if ( 1.0 < x * 2 / (float)SIZEW ) x = 0.99 / 2.0 * (float)SIZEW;
        if ( 0.0001 > 1.0-(y * 2 / (float)SIZEH ) ) y = -( 0.0001 - 1.0 ) / 2.0 * (float) SIZEH;
        if ( 1.0 < 1.0-(y * 2 / (float)SIZEH ) ) y = 0.0;

        int i;
        //Search the right position for the grabbed point
        for( i = 0; i < line->count()-1; i++ )
        {
            if( line->at(i).x() > x * 2 / (float)SIZEW )
            {
                break;
            }
        }
        //insert it for drawing
        line->insert( i, QPointF( x * 2 / (float)SIZEW,
                                  1.0-((y * 2 + HALFSIZEH) / (float)SIZEH )) );
        paintElement();
        //and grab it again
        if( release ) line->removeAt( i );
    }
    //Drag first & last point up & down
    else if( ( m_firstPoint || m_lastPoint ) && ( m_diagramType != LuminanceVsSaturation ) )
    {
        float yNew = 1.0-((y * 2 + HALFSIZEH) / (float)SIZEH );
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
        float yNew = 1.0-((y * 2 + HALFSIZEH) / (float)SIZEH );
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
    m_width = size().width();
    paintElement();
    event->accept();
}
