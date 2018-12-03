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
//#include "../../src/processing/spline/spline_helper.h"


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
    resetLines();
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

    //Paint to label
    QPixmap pic = QPixmap::fromImage( *m_pImage ).scaled( HALFSIZE * devicePixelRatio(), (HALFSIZE) * devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation );
    pic.setDevicePixelRatio( devicePixelRatio() );
    setPixmap( pic );
}

//Reset the lines
void HueVsSat::resetLines()
{
    initLine( &m_whiteLine );
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
    int ret = 0;//spline1dc( pXin , pYin , &numIn, pXout, pYout, &numOut );
    //qDebug() << ret;

    //Draw the line
    if( ret == 0 )
    {
        pPainter->setPen( QPen( QBrush( color ), 2 ) );
        if( pYout[0] > 1.0 ) pYout[0] = 1.0;
        else if( pYout[0] < 0.0001 ) pYout[0] = 0.0001;
        for( int i = 1; i < numOut; i++ )
        {
            if( pYout[i] > 1.0 ) pYout[i] = 1.0;
            else if( pYout[i] < 0.0001 ) pYout[i] = 0.0001;
            pPainter->drawLine( pXout[i-1]*SIZE, SIZE-(pYout[i-1]*SIZE), pXout[i]*SIZE, SIZE-(pYout[i]*SIZE) );
        }
    }

    //Draw Points
    if( active )
    {
        pPainter->setPen( QPen( QBrush( color ), 1 ) );
        pPainter->setBrush( color );
        for( int i = 0; i < line.count(); i++ )
        {
            pPainter->drawEllipse( pXin[i]*SIZE-7, SIZE-(pYin[i]*SIZE)-7, 15, 15 );
        }
    }

    //Set Procesing Element if available
    if( active || m_LoadAll )
    {
        //if( m_pProcessing != NULL ) processingSetGCurve( m_pProcessing, numIn, pXin, pYin, channel );
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
    line->append( QPointF( 0.00001, 0.0 ) );
    line->append( QPointF( 1.0, 0.0 ) );
}
