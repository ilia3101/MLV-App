/*!
 * \file GraphicsZoomView.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A QGraphicsView wihtout scrolling but with zoom on mousewheel or y-axis on trackpad
 */

#include "GraphicsZoomView.h"

//Constructor
GraphicsZoomView::GraphicsZoomView(QWidget* parent, Qt::WindowFlags f)
    : QGraphicsView(parent)
{
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_isZoomEnabled = false;
}

//En-/disable zoom on mouse wheel
void GraphicsZoomView::setZoomEnabled(bool on)
{
    m_isZoomEnabled = on;
}

//Reset the zoom to exact 100%
void GraphicsZoomView::resetZoom()
{
    resetTransform();
}

//Methods for changing the cursor
void GraphicsZoomView::enterEvent(QEvent *event)
{
    QGraphicsView::enterEvent(event);
    //viewport()->setCursor(Qt::CrossCursor);
}

void GraphicsZoomView::mousePressEvent(QMouseEvent *event)
{
    QGraphicsView::mousePressEvent(event);
    //viewport()->setCursor(Qt::CrossCursor);
}

void GraphicsZoomView::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
    //viewport()->setCursor(Qt::CrossCursor);
}

//The mousewheel event
void GraphicsZoomView::wheelEvent(QWheelEvent *event)
{
    //If disabled, do nothing
    if( !m_isZoomEnabled ) return;

    //else zoom
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Scale the view / do the zoom
    double scaleFactor = 1.05;
    if(event->angleDelta().y() > 0)
    {
        // Zoom in
        scale(scaleFactor, scaleFactor);
    }
    else if(event->angleDelta().y() < 0)
    {
        // Zooming out
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
    else
    {
        //do nothing
    }
}
