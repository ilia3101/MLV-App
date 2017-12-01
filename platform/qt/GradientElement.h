/*!
 * \file GradientElement.h
 * \author masc4ii
 * \copyright 2017
 * \brief Functions to realize a gradient element, which works in a stretchable viewer
 */

#ifndef GRADIENTELEMENT_H
#define GRADIENTELEMENT_H

#include "GraphicsPolygonMoveItem.h"

class GradientElement
{
public:
    GradientElement( QPolygon polygon, QGraphicsItem *parent = 0 );
    ~GradientElement();
    void setStartPos( double x, double y );
    void setFinalPos( double x, double y );
    void setUiLength( int uiLength );
    void setUiAngle( double uiAngle );
    void setStrechFactorY( double factor );
    void reset();

    int uiLength( void );
    int stretchedLength( void );
    double uiAngle( void );
    double stretchedAngle( void );

    GraphicsPolygonMoveItem* gradientGraphicsElement( void );
    void redrawGradientElement( int sceneX, int sceneY, int picX, int picY );

private:
    void calcFromPoints();
    void createGradientElement( int scaledLength );
    double m_startX;
    double m_startY;
    double m_endX;
    double m_endY;
    double m_lengthUi;
    double m_lengthStretched;
    double m_angleUi;
    double m_angleStretched;
    double m_stretchFactorY;
    GraphicsPolygonMoveItem *m_pGradientGraphicsItem;
};

#endif // GRADIENTELEMENT_H
