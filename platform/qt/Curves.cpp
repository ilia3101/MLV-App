/*!
 * \file Curves.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief The gradation curves element
 */

#include "Curves.h"
#include <QPainter>
#include <QDebug>
#include <QMouseEvent>
#include "../../src/processing/interpolation/spline_helper.h"


#define SIZEW                this->width()
#define SIZEH                SIZEW
#define HALFSIZEW            (SIZEW/2)
#define HALFSIZEH            (SIZEH/2)
#define QUARTERSIZEW         (SIZEW/4)
#define QUARTERSIZEH         (SIZEH/4)
#define THREEQUARTERSIZEW    (SIZEW*3/4)
#define THREEQUARTERSIZEH    (SIZEH*3/4)

#define POINT_RADIUS         4.0
#define GRAB_RADIUS          (POINT_RADIUS + 2.0)

//Constructor
Curves::Curves(QWidget *parent)
    : QLabel(parent) {
    m_cursor = QPoint( 0, 0 );
    m_pointSelected = false;
    m_activeLine = LINENR_W;
    m_pProcessing = NULL;
    m_pFrameChanged = NULL;
    m_LoadAll = false;
    resetLines();
}

//Destructor
Curves::~Curves()
{
}

//Set the processing element that this class can control it
void Curves::setProcessingObject(processingObject_t *processing)
{
    m_pProcessing = processing;
}

//Draw the curves object on a label
void Curves::paintElement(bool frameChanged)
{
    QImage image = QImage( SIZEW * devicePixelRatio(), SIZEH * devicePixelRatio(), QImage::Format_RGB32 );
    image.setDevicePixelRatio( devicePixelRatio() );

    image.fill( QColor( 0, 0, 0, 255 ) );
    QPainter painterTc( &image );
    painterTc.setPen( Qt::NoPen );

    //Lines
    painterTc.setPen( QColor( 64, 64, 64, 255 ) );
    painterTc.drawLine( HALFSIZEW  , 0, HALFSIZEW  , SIZEH );
    painterTc.drawLine( QUARTERSIZEW  , 0, QUARTERSIZEW  , SIZEH );
    painterTc.drawLine( THREEQUARTERSIZEW  , 0, THREEQUARTERSIZEW  , SIZEH );

    painterTc.drawLine( 0, HALFSIZEH  , SIZEW, HALFSIZEH   );
    painterTc.drawLine( 0, QUARTERSIZEH  , SIZEW, QUARTERSIZEH   );
    painterTc.drawLine( 0, THREEQUARTERSIZEH  , SIZEW, THREEQUARTERSIZEH   );

    painterTc.setRenderHint( QPainter::Antialiasing );

    //Paint line in right order
    if( m_activeLine == LINENR_W )
    {
        paintLine( m_blueLine, &painterTc, QColor( 32, 160, 255, 255 ), false, LINENR_B );
        paintLine( m_greenLine, &painterTc, QColor( 0, 255, 0, 255 ), false, LINENR_G );
        paintLine( m_redLine, &painterTc, QColor( 255, 0, 0, 255 ), false, LINENR_R );
        paintLine( m_whiteLine, &painterTc, QColor( 255, 255, 255, 255 ), true, LINENR_W );
    }
    else if( m_activeLine == LINENR_R )
    {
        paintLine( m_blueLine, &painterTc, QColor( 32, 160, 255, 255 ), false, LINENR_B );
        paintLine( m_greenLine, &painterTc, QColor( 0, 255, 0, 255 ), false, LINENR_G );
        paintLine( m_whiteLine, &painterTc, QColor( 255, 255, 255, 255 ), false, LINENR_W );
        paintLine( m_redLine, &painterTc, QColor( 255, 0, 0, 255 ), true, LINENR_R );
    }
    else if( m_activeLine == LINENR_G )
    {
        paintLine( m_blueLine, &painterTc, QColor( 32, 160, 255, 255 ), false, LINENR_B );
        paintLine( m_redLine, &painterTc, QColor( 255, 0, 0, 255 ), false, LINENR_R );
        paintLine( m_whiteLine, &painterTc, QColor( 255, 255, 255, 255 ), false, LINENR_W );
        paintLine( m_greenLine, &painterTc, QColor( 0, 255, 0, 255 ), true, LINENR_G );
    }
    else //LINENR_B
    {
        paintLine( m_greenLine, &painterTc, QColor( 0, 255, 0, 255 ), false, LINENR_G );
        paintLine( m_redLine, &painterTc, QColor( 255, 0, 0, 255 ), false, LINENR_R );
        paintLine( m_whiteLine, &painterTc, QColor( 255, 255, 255, 255 ), false, LINENR_W );
        paintLine( m_blueLine, &painterTc, QColor( 32, 160, 255, 255 ), true, LINENR_B );
    }

    //Paint to label
    QPixmap pic = QPixmap::fromImage( image );
    setPixmap( pic );

    setMinimumWidth( 1 ); //Otherwise window won't be smaller than picture

    if( frameChanged && m_pFrameChanged != NULL ) *m_pFrameChanged = true;
}

//Set active line
void Curves::setActiveLine(uint8_t lineNr)
{
    m_activeLine = lineNr;
    paintElement( false );
}

//Set the flag for rendering the viewers picture
void Curves::setFrameChangedPointer(bool *pFrameChanged)
{
    m_pFrameChanged = pFrameChanged;
}

//Reset the lines
void Curves::resetLines()
{
    initLine( &m_whiteLine );
    initLine( &m_redLine );
    initLine( &m_greenLine );
    initLine( &m_blueLine );
    if( m_pProcessing != NULL )
    {
        processingSetGCurve( m_pProcessing, 0, NULL, NULL, 0 );
        processingSetGCurve( m_pProcessing, 0, NULL, NULL, 1 );
        processingSetGCurve( m_pProcessing, 0, NULL, NULL, 2 );
        processingSetGCurve( m_pProcessing, 0, NULL, NULL, 3 );
    }
}

//Reset the current line
void Curves::resetCurrentLine()
{
    QVector<QPointF> *line = getActiveLinePointer();
    initLine( line );
    if( m_pProcessing != NULL )
    {
        processingSetGCurve( m_pProcessing, 0, NULL, NULL, m_activeLine );
    }
}

//Load configuration string to settings
void Curves::setConfiguration(QString config)
{
    //qDebug() << "Load gradation curve:" << config;
    for( int i = 0; i < 4; i++ )
    {
        if( config.size() <= 0 ) return;
        QVector<QPointF> *line;
        if( i == 0 ) line = &m_whiteLine;
        else if( i == 1 ) line = &m_redLine;
        else if( i == 2 ) line = &m_greenLine;
        else line = &m_blueLine;

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
        config.remove( 0, 1 ); //remove "?"
    }
    m_LoadAll = true;
    paintElement();
    m_LoadAll = false;
}

//Get a configuration string to save settings
QString Curves::configuration()
{
    QString config;
    config.clear();

    for( int i = 0; i < m_whiteLine.size(); i++ )
    {
        config.append( QString( "%1;%2;" ).arg( m_whiteLine.at(i).x() ).arg( m_whiteLine.at(i).y() ) );
    }
    config.append( QString( "?" ) );
    for( int i = 0; i < m_redLine.size(); i++ )
    {
        config.append( QString( "%1;%2;" ).arg( m_redLine.at(i).x() ).arg( m_redLine.at(i).y() ) );
    }
    config.append( QString( "?" ) );
    for( int i = 0; i < m_greenLine.size(); i++ )
    {
        config.append( QString( "%1;%2;" ).arg( m_greenLine.at(i).x() ).arg( m_greenLine.at(i).y() ) );
    }
    config.append( QString( "?" ) );
    for( int i = 0; i < m_blueLine.size(); i++ )
    {
        config.append( QString( "%1;%2;" ).arg( m_blueLine.at(i).x() ).arg( m_blueLine.at(i).y() ) );
    }
    //qDebug() << "Save gradation curve:" << config;
    return config;
}

//Mouse pressed -> select a point
void Curves::mousePressEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = getActiveLinePointer();

    QPointF mousePos = mouse->localPos();

    double mx = mousePos.x();
    double my = SIZEH - mousePos.y();

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
void Curves::mouseDoubleClickEvent(QMouseEvent *mouse)
{
    QVector<QPointF> *line = getActiveLinePointer();

    QPointF mousePos = mouse->localPos();

    double mx = mousePos.x();
    double my = SIZEH - mousePos.y();

    if( std::abs( mx - ( line->at( 0 ).x() * SIZEW ) ) <= GRAB_RADIUS
        && std::abs( my - ( line->at( 0 ).y() * SIZEH ) ) <= GRAB_RADIUS )
    {
        QPointF point = line->takeFirst();
        point.setY( 0 );
        line->prepend( point );

        paintElement();
        return;
    }

    if( std::abs( mx - ( line->at( line->count() - 1 ).x() * SIZEW ) ) <= GRAB_RADIUS
        && std::abs( my - ( line->at( line->count() - 1 ).y() * SIZEH ) ) <= GRAB_RADIUS )
    {
        QPointF point = line->takeLast();
        point.setY( 1 );
        line->append( point );

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
void Curves::mouseReleaseEvent(QMouseEvent *mouse)
{
    movePoint( mouse->localPos().x(), mouse->localPos().y(), false );
    m_pointSelected = false;
    m_firstPoint = false;
    m_lastPoint = false;
}

//Mouse moved -> move marker
void Curves::mouseMoveEvent(QMouseEvent *mouse)
{
    movePoint( mouse->localPos().x(), mouse->localPos().y(), true );
}

//Resize element
void Curves::resizeEvent(QResizeEvent *event)
{
    paintElement( false );
    event->accept();
}

//Paint line
void Curves::paintLine(QVector<QPointF> line, QPainter *pPainter, QColor color, bool active, uint8_t channel)
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
    int ret = spline1dc( pXin , pYin , &numIn, pXout, pYout, &numOut );
    //qDebug() << ret;

    //Draw the line
    if( ret == 0 )
    {
        pPainter->setPen( QPen( QBrush( color ), 1.01 ) );
        if( pYout[0] > 1.0 ) pYout[0] = 1.0;
        else if( pYout[0] < 0.0001 ) pYout[0] = 0.0001;
        for( int i = 1; i < numOut; i++ )
        {
            if( pYout[i] > 1.0 ) pYout[i] = 1.0;
            else if( pYout[i] < 0.0001 ) pYout[i] = 0.0001;
            pPainter->drawLine( QPointF( pXout[i-1]*SIZEW, SIZEH*(1.0-pYout[i-1]) ), QPointF( pXout[i]*SIZEW, SIZEH*(1.0-pYout[i]) ) );
        }
    }

    //Draw Points
    if( active )
    {
        pPainter->setPen( Qt::NoPen );
        pPainter->setBrush( color );
        for( int i = 0; i < line.size(); i++ )
        {
            pPainter->drawEllipse( QPointF( pXin[i]*SIZEW, SIZEH*(1.0-pYin[i]) ), POINT_RADIUS, POINT_RADIUS );
        }
    }

    //Set Procesing Element if available
    if( active || m_LoadAll )
    {
        if( m_pProcessing != NULL ) processingSetGCurve( m_pProcessing, numIn, pXin, pYin, channel );
    }

    //CleanUp
    free( pXin );
    free( pYin );
    free( pXout );
    free( pYout );
}

//Init / delete line
void Curves::initLine(QVector<QPointF> *line)
{
    line->clear();
    line->append( QPointF( 0.0, 0.0 ) );
    line->append( QPointF( 1.0, 1.0 ) );
}

//Move a point around
void Curves::movePoint(qreal x, qreal y, bool release)
{
    QVector<QPointF> *line = getActiveLinePointer();
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
                                  1.0 - ( y / (float)SIZEH ) ) );
        paintElement();
        //and grab it again
        if( release ) line->removeAt( i );
    }
    //Drag first point up & down
    else if( m_firstPoint )
    {
        float yNew = 1.0 - ( y / (float)SIZEH );
        QPointF point = line->takeFirst();
        if( yNew > 1.0 ) yNew = 1.0;
        else if( yNew < 0.0001 ) yNew = 0.0;
        point.setY( yNew );
        line->prepend( point );
        paintElement();
    }
    //Drag last point up & down
    else if( m_lastPoint )
    {
        float yNew = 1.0 - ( y / (float)SIZEH );
        QPointF point = line->takeLast();
        if( yNew > 1.0 ) yNew = 1.0;
        else if( yNew < 0.0001 ) yNew = 0.0;
        point.setY( yNew );
        line->append( point );
        paintElement();
    }
}

//Get a pointer to the active line
QVector<QPointF> *Curves::getActiveLinePointer()
{
    if( m_activeLine == LINENR_R ) return &m_redLine;
    else if( m_activeLine == LINENR_G ) return &m_greenLine;
    else if( m_activeLine == LINENR_B ) return &m_blueLine;
    else return &m_whiteLine;
}
