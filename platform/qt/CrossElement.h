/*!
 * \file CrossElement.h
 * \author masc4ii
 * \copyright 2019
 * \brief A vector cross element needed for bad pixel visualization
 */

#ifndef CROSSELEMENT_H
#define CROSSELEMENT_H

#include <QPoint>
#include <QGraphicsPathItem>

class CrossElement
{
public:
    CrossElement( QPainterPath path, QGraphicsItem *parent = 0 );
    ~CrossElement();
    void setPosition( double x, double y );
    void reset();

    QGraphicsPathItem* crossGraphicsElement( void );
    void redrawCrossElement( int sceneX, int sceneY, int picX, int picY );

private:
    void calcFromPoints();
    void createCrossElement();
    double m_posX;
    double m_posY;
    QGraphicsPathItem *m_pCrossGraphicsItem;
};

#endif // CROSSELEMENT_H
