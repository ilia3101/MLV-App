/*!
 * \file CrossElement.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief A vector cross element needed for bad pixel visualization
 */

#include "CrossElement.h"
#include <QPen>

#define CROSS_LENGTH 3

//Constructor
CrossElement::CrossElement(QPolygon polygon, QGraphicsItem *parent)
{
    m_pCrossGraphicsItem = new QGraphicsPolygonItem( polygon, parent );
    QPen pen = QPen( Qt::white );
    pen.setWidth( 0 );
    m_pCrossGraphicsItem->setPen( pen );
    m_pCrossGraphicsItem->setFlag( QGraphicsItem::ItemIsMovable, false );
    m_pCrossGraphicsItem->setFlag( QGraphicsItem::ItemSendsScenePositionChanges, true );
    reset();
}

//Destructor
CrossElement::~CrossElement()
{
    delete m_pCrossGraphicsItem;
}

//Set position on image
void CrossElement::setPosition(double x, double y)
{
    m_posX = x + 0.5;
    m_posY = y + 0.5;
}

//Reset all members
void CrossElement::reset()
{
    m_posX = 0.0;
    m_posY = 0.0;
    m_pCrossGraphicsItem->hide();
}

//Get the cross graphics element
QGraphicsPolygonItem *CrossElement::crossGraphicsElement()
{
    return m_pCrossGraphicsItem;
}

//Draw the element to a position, with length and angle
void CrossElement::redrawCrossElement(int sceneX, int sceneY, int picX, int picY)
{
    m_pCrossGraphicsItem->setPos( m_posX * sceneX / picX,
                                  m_posY * sceneY / picY );
    createCrossElement();
}

//Create the graphical cross element
void CrossElement::createCrossElement()
{
    QPolygon polygon;
    polygon << QPoint(0, 0)
            << QPoint(CROSS_LENGTH, 0)
            << QPoint(-CROSS_LENGTH, 0)
            << QPoint(0, 0)
            << QPoint(0, CROSS_LENGTH)
            << QPoint(0, -CROSS_LENGTH)
            << QPoint(0, 0);
    m_pCrossGraphicsItem->setPolygon( polygon );
}
