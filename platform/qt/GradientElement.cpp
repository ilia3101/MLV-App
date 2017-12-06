/*!
 * \file GradientElement.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief Functions to realize a gradient element, which works in a stretchable viewer. There are 2 coordinates: one for the ui and one for the viewer. The viewer becomes different if stretched.
 */

#include "GradientElement.h"
#include "math.h"
#include <QPen>

//Constructor
GradientElement::GradientElement(QPolygon polygon, QGraphicsItem *parent)
{
    m_pGradientGraphicsItem = new GraphicsPolygonMoveItem( polygon, parent );
    QPen pen = QPen( Qt::white );
    pen.setWidth( 0 );
    m_pGradientGraphicsItem->setPen( pen );
    m_pGradientGraphicsItem->setFlag( QGraphicsItem::ItemIsMovable, true );
    m_pGradientGraphicsItem->setFlag( QGraphicsItem::ItemSendsScenePositionChanges, true );
    m_stretchFactorX = 1.0;
    m_stretchFactorY = 1.0;
    reset();
}

//Destructor
GradientElement::~GradientElement()
{
    delete m_pGradientGraphicsItem;
}

//Reset all and set start pos
void GradientElement::setStartPos(double x, double y)
{
    m_endX -= m_startX - x;
    m_endY -= m_startY - y;
    m_startX = x;
    m_startY = y;
}

//Set final pos and calc
void GradientElement::setFinalPos(double x, double y)
{
    m_endX = x;
    m_endY = y;

    calcFromPoints();
}

//Set the UI length, calc all other values
void GradientElement::setUiLength(int uiLength)
{
    //calc final x,y with ui angle
    m_endX = m_startX + ( uiLength * sin( m_angleUi * M_PI / 180.0 ) );
    m_endY = m_startY - ( uiLength * cos( m_angleUi * M_PI / 180.0 ) );

    //calc
    calcFromPoints();
}

//Set the UI angle, calc all other values
void GradientElement::setUiAngle(double uiAngle)
{
    //calc final x,y with ui length
    m_endX = m_startX + ( m_lengthUi * sin( uiAngle * M_PI / 180.0 ) );
    m_endY = m_startY - ( m_lengthUi * cos( uiAngle * M_PI / 180.0 ) );

    //calc
    calcFromPoints();
}

//Get the stretch X factor to this class
void GradientElement::setStrechFactorX(double factor)
{
    m_stretchFactorX = factor;

    //calc
    calcFromPoints();
}

//Get the stretch Y factor to this class
void GradientElement::setStrechFactorY(double factor)
{
    m_stretchFactorY = factor;

    //calc
    calcFromPoints();
}

//Get length for UI element numbers
int GradientElement::uiLength()
{
    return m_lengthUi;
}

//Get length for viewer
int GradientElement::stretchedLength()
{
    return m_lengthStretched;
}

//Get angle for UI element numbers
double GradientElement::uiAngle()
{
    return m_angleUi;
}

//Get angle for viewer
double GradientElement::stretchedAngle()
{
    return m_angleStretched;
}

//Get the gradient graphics element
GraphicsPolygonMoveItem *GradientElement::gradientGraphicsElement()
{
    return m_pGradientGraphicsItem;
}

//Create the graphical gradient element
void GradientElement::createGradientElement( int scaledLength )
{
    QPolygon polygon;
    polygon << QPoint(0, -scaledLength)
            << QPoint(-10000, -scaledLength)
            << QPoint(10000, -scaledLength)
            << QPoint(0, -scaledLength)
            << QPoint(-10, -scaledLength+10)
            << QPoint(10, -scaledLength+10)
            << QPoint(0, -scaledLength)
            << QPoint(0, 0)
            << QPoint(-10000, 0)
            << QPoint(10000, 0)
            << QPoint(0, 0);
    m_pGradientGraphicsItem->setPolygon( polygon );
}

//Draw the element to a position, with length and angle
void GradientElement::redrawGradientElement(int sceneX, int sceneY, int picX, int picY)
{
    m_pGradientGraphicsItem->blockSignals( true );
    m_pGradientGraphicsItem->setPos( m_startX * sceneX / picX,
                                     m_startY * sceneY / picY );
    m_pGradientGraphicsItem->setRotation( m_angleStretched );
    createGradientElement( m_lengthStretched * sceneX / picX );
    m_pGradientGraphicsItem->blockSignals( false );
}

//Calc both, ui numbers and viewer numbers
void GradientElement::calcFromPoints()
{
    //Calc UI numbers
    double m = ( m_endY - m_startY ) / ( m_endX - m_startX );
    m_lengthUi = sqrt( pow( m_endX - m_startX, 2 ) + pow( m_endY - m_startY, 2 ) );
    if( m != 0 )
    {
        m_angleUi = ( atan( m ) * 180.0 / M_PI ) - 90.0;
        if( ( m_endX - m_startX ) >= 0 ) m_angleUi += 180;
    }
    else if( ( m_endY - m_startY ) == 0 )
    {
        if( ( m_endX - m_startX ) > 0 ) m_angleUi = 90;
        else m_angleUi = -90;
    }

    //Calc Viewer numbers
    double startXstretched = m_startX * m_stretchFactorX;
    double endXstretched = m_endX * m_stretchFactorX;
    double startYstretched = m_startY * m_stretchFactorY;
    double endYstretched = m_endY * m_stretchFactorY;

    m = ( endYstretched - startYstretched ) / ( endXstretched - startXstretched );
    m_lengthStretched = sqrt( pow( endXstretched - startXstretched, 2 ) + pow( endYstretched - startYstretched, 2 ) ) / m_stretchFactorX;
    if( m != 0 )
    {
        m_angleStretched = ( atan( m ) * 180.0 / M_PI ) - 90.0;
        if( ( endXstretched - startXstretched ) >= 0 ) m_angleStretched += 180;
    }
    else if( ( endYstretched - startYstretched ) == 0 )
    {
        if( ( endXstretched - startXstretched ) > 0 ) m_angleStretched = 90;
        else m_angleStretched = -90;
    }
}

//Reset all members
void GradientElement::reset()
{
    m_startX = 0;
    m_startY = 0;
    m_endX = 0;
    m_endY = 0;
    m_angleUi = 0;
    m_lengthUi = 0;
    m_angleStretched = 0;
    m_lengthStretched = 0;
    m_pGradientGraphicsItem->hide();
}
