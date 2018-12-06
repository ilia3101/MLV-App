/*!
 * \file HueVsSat.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief The gradation HueVsSat element
 */

#include "HueVsSat.h"
#include <QPainter>
#include <QDebug>
#include <QMouseEvent>
#include "../../src/processing/interpolation/cosine_interpolation.h"

#define SIZE                360
#define HALFSIZE            (SIZE/2)
#define QUARTERSIZE         (SIZE/4)
#define THREEQUARTERSIZE    (SIZE*3/4)

//Constructor
HueVsSat::HueVsSat(QWidget *parent)
    : QLabel(parent) {
    m_pImage = new QImage( SIZE, SIZE, QImage::Format_RGBA8888 );
    m_cursor = QPoint( 0, 0 );
    m_pointSelected = false;
    m_pFrameChanged = NULL;
    m_LoadAll = false;
    resetLine();
}

//Destructor
HueVsSat::~HueVsSat()
{
    delete m_pImage;
}

//Set processing object to this class
void HueVsSat::setProcessingObject(processingObject_t *processing)
{
    m_pProcessing = processing;
}

//Set the flag for rendering the viewers picture
void HueVsSat::setFrameChangedPointer(bool *pFrameChanged)
{
    m_pFrameChanged = pFrameChanged;
}

//Draw the HueVsSat object on a label
void HueVsSat::paintElement()
{
    m_pImage->fill( QColor( 0, 0, 0, 255 ) );
    QPainter painterTc( m_pImage );
    painterTc.setRenderHint(QPainter::Antialiasing);

    //Color
    QLinearGradient gradient1( 0, 0, SIZE, 0 );
    gradient1.setColorAt( 0, QColor( 255, 0, 0, 255 ) );
    gradient1.setColorAt( 0.18, QColor( 255, 255, 0, 255 ) );
    gradient1.setColorAt( 0.375, QColor( 0, 255, 0, 255 ) );
    gradient1.setColorAt( 0.5, QColor( 0, 255, 255, 255 ) );
    gradient1.setColorAt( 0.68, QColor( 0, 0, 255, 255 ) );
    gradient1.setColorAt( 0.875, QColor( 255, 0, 255, 255 ) );
    gradient1.setColorAt( 1, QColor( 255, 0, 0, 255 ) );
    painterTc.setBrush( gradient1 );
    painterTc.drawRect( 0, 0, SIZE, SIZE );

    //HueVsSat
    /*QLinearGradient gradient2( 0, 0, 0, SIZE );
    gradient2.setColorAt( 0, QColor( 128, 128, 128, 0 ) );
    gradient2.setColorAt( 1, QColor( 128, 128, 128, 255 ) );
    painterTc.setBrush( gradient2 );
    painterTc.drawRect( 0, 0, SIZE, SIZE );*/

    //HueVsLuminance
    QLinearGradient gradient2( 0, 0, 0, SIZE );
    gradient2.setColorAt( 0, QColor( 255, 255, 255, 255 ) );
    gradient2.setColorAt( 0.5, QColor( 128, 128, 128, 0 ) );
    gradient2.setColorAt( 1, QColor( 0, 0, 0, 255 ) );
    painterTc.setBrush( gradient2 );
    painterTc.drawRect( 0, 0, SIZE, SIZE );

    //Lines
    painterTc.setPen( QColor( 100, 100, 100, 255 ) );
    for( int i = 1; i < 3; i++ )
    {
        painterTc.drawLine( (i*SIZE/3)-1, 0, (i*SIZE/3)-1, SIZE );
        painterTc.drawLine( (i*SIZE/3)  , 0, (i*SIZE/3)  , SIZE );
    }

    for( int i = 1; i < 4; i++ )
    {
        painterTc.drawLine( 0, (i*SIZE/4)-1, SIZE, (i*SIZE/4)-1 );
        painterTc.drawLine( 0, (i*SIZE/4)  , SIZE, (i*SIZE/4)   );
    }

    paintLine( m_whiteLine, &painterTc, QColor(255,255,255,255), true, 0 );

    //Paint to label
    QPixmap pic = QPixmap::fromImage( *m_pImage ).scaled( HALFSIZE * devicePixelRatio(), (HALFSIZE) * devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation );
    pic.setDevicePixelRatio( devicePixelRatio() );
    setPixmap( pic );
}

//Reset the lines
void HueVsSat::resetLine()
{
    initLine( &m_whiteLine );
}

//Load configuration string to settings
void HueVsSat::setConfiguration(QString config)
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
QString HueVsSat::configuration()
{
    QString config;
    config.clear();

    for( int i = 0; i < m_whiteLine.count(); i++ )
    {
        config.append( QString( "%1;%2;" ).arg( m_whiteLine.at(i).x() ).arg( m_whiteLine.at(i).y() ) );
    }

    return config;
}

//Paint line
void HueVsSat::paintLine(QVector<QPointF> line, QPainter *pPainter, QColor color, bool active, uint8_t channel)
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
    float *pXout = (float*)malloc( sizeof(float) * SIZE );
    float *pYout = (float*)malloc( sizeof(float) * SIZE );

    int numIn = line.count();
    int numOut = SIZE;

    //Data into x of output sets
    for( int i = 0; i < numOut; i++ )
    {
        pXout[i] = i / (float)SIZE;
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
            pPainter->drawLine( pXout[i-1]*SIZE, HALFSIZE-(pYout[i-1]*SIZE), pXout[i]*SIZE, HALFSIZE-(pYout[i]*SIZE) );
        }
    }

    //Draw Points
    if( active )
    {
        pPainter->setPen( QPen( QBrush( color ), 1 ) );
        pPainter->setBrush( color );
        for( int i = 0; i < line.count(); i++ )
        {
            pPainter->drawEllipse( pXin[i]*SIZE-7, HALFSIZE-(pYin[i]*SIZE)-7, 15, 15 );
        }
    }

    //Set Procesing Element if available
    if( active || m_LoadAll )
    {
        if( m_pProcessing != NULL ) processingSetHueVsLuma( m_pProcessing, numIn, pXin, pYin );
        if( m_pFrameChanged != NULL ) *m_pFrameChanged = true;
    }

    //CleanUp
    free( pXin );
    free( pYin );
    free( pXout );
    free( pYout );
}

//Init / delete line
void HueVsSat::initLine(QVector<QPointF> *line)
{
    line->clear();
    line->append( QPointF( 0.0, 0.0 ) );
    line->append( QPointF( 1.0, 0.0 ) );
}

//Mouse pressed -> select a point
void HueVsSat::mousePressEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = &m_whiteLine;

    if( ( mouse->localPos().x() ) < line->at(0).x() / 2 * (float)SIZE+10
     && ( mouse->localPos().x() ) > line->at(0).x() / 2 * (float)SIZE-10
     && ( QUARTERSIZE-mouse->localPos().y() ) < (line->at(0).y() / 2 * (float)SIZE+10)
     && ( QUARTERSIZE-mouse->localPos().y() ) > (line->at(0).y() / 2 * (float)SIZE-10) )
    {
        m_firstPoint = true;
    }
    else if( ( mouse->localPos().x() ) < line->at(line->count()-1).x() / 2 * (float)SIZE+10
     && ( mouse->localPos().x() ) > line->at(line->count()-1).x() / 2 * (float)SIZE-10
     && ( QUARTERSIZE-mouse->localPos().y() ) < (line->at(line->count()-1).y() / 2 * (float)SIZE+10)
     && ( QUARTERSIZE-mouse->localPos().y() ) > (line->at(line->count()-1).y() / 2 * (float)SIZE-10) )
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
            if( ( mouse->localPos().x() ) < line->at(i).x() / 2 * (float)SIZE+10
             && ( mouse->localPos().x() ) > line->at(i).x() / 2 * (float)SIZE-10
             && ( QUARTERSIZE-mouse->localPos().y() ) < (line->at(i).y() / 2 * (float)SIZE+10)
             && ( QUARTERSIZE-mouse->localPos().y() ) > (line->at(i).y() / 2 * (float)SIZE-10) )
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
void HueVsSat::mouseDoubleClickEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = &m_whiteLine;
    for( int i = 1; i < line->count()-1; i++ )
    {
        if( ( mouse->localPos().x() ) < line->at(i).x() / 2 * (float)SIZE+10
         && ( mouse->localPos().x() ) > line->at(i).x() / 2 * (float)SIZE-10
         && ( QUARTERSIZE-mouse->localPos().y() ) < (line->at(i).y() / 2 * (float)SIZE+10)
         && ( QUARTERSIZE-mouse->localPos().y() ) > (line->at(i).y() / 2 * (float)SIZE-10) )
        {
            line->removeAt( i );
            paintElement();
            break;
        }
    }
}

//Mouse released -> deselect point
void HueVsSat::mouseReleaseEvent(QMouseEvent *mouse)
{
    movePoint( mouse->localPos().x(), mouse->localPos().y(), false );
    m_pointSelected = false;
    m_firstPoint = false;
    m_lastPoint = false;
}

//Mouse moved -> move marker
void HueVsSat::mouseMoveEvent(QMouseEvent *mouse)
{
    movePoint( mouse->localPos().x(), mouse->localPos().y(), true );
}

//Move a point around
void HueVsSat::movePoint(qreal x, qreal y, bool release)
{
    QVector<QPointF> *line = &m_whiteLine;
    //move the marker
    if( m_pointSelected )
    {
        if( ( 0.0001 > x * 2 / (float)SIZE )
         || ( 1.0 < x * 2 / (float)SIZE )
         || ( 0.0001 > 1.0-(y * 2 / (float)SIZE ) )
         || ( 1.0 < 1.0-(y * 2 / (float)SIZE ) ) )
        {
            paintElement();
        }
        else
        {
            int i;
            //Search the right position for the grabbed point
            for( i = 0; i < line->count()-1; i++ )
            {
                if( line->at(i).x() > x * 2 / (float)SIZE )
                {
                    break;
                }
            }
            //insert it for drawing
            line->insert( i, QPointF( x * 2 / (float)SIZE,
                                      1.0-((y * 2 + HALFSIZE) / (float)SIZE )) );
            paintElement();
            //and grab it again
            if( release ) line->removeAt( i );
        }
    }
    //Drag first & last point up & down
    else if( m_firstPoint || m_lastPoint )
    {
        float yNew = 1.0-((y * 2 + HALFSIZE) / (float)SIZE );
        QPointF point = line->takeFirst();
        if( yNew > 1.0 ) yNew = 1.0;
        else if( yNew < -1.0 ) yNew = -1.0;
        point.setY( yNew );
        line->prepend( point );
        point = line->takeLast();
        point.setY( yNew );
        line->append( point );
        paintElement();
    }
}
