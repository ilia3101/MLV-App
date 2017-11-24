/*!
 * \file GraphicsZoomView.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A QGraphicsView without scrolling but with zoom on mousewheel or y-axis on trackpad
 */

#include "GraphicsZoomView.h"
#include <QCursor>
#include <QPixmap>

//Constructor
GraphicsZoomView::GraphicsZoomView(QWidget *parent) :
    QGraphicsView(parent)
{
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_isZoomEnabled = false;
    m_isWbPickerActive = false;
    m_isMousePressed = false;
}

//En-/disable zoom on mouse wheel
void GraphicsZoomView::setZoomEnabled(bool on)
{
    m_isZoomEnabled = on;
}

//return if zoom on mouse wheel is en-/disabled
bool GraphicsZoomView::isZoomEnabled()
{
    return m_isZoomEnabled;
}

//Reset the zoom to exact 100%
void GraphicsZoomView::resetZoom()
{
    //resetTransform(); //->kills anchor
    //Calculating this does not kill anchor -> latest anchor will be used
    qreal percentZoom = 100.0;
    qreal targetScale = (qreal)percentZoom / 100.0;
    qreal scaleFactor = targetScale / transform().m11();
    scale( scaleFactor, scaleFactor );
}

//Set white balance picker active
void GraphicsZoomView::setWbPickerActive(bool on)
{
    m_isWbPickerActive = on;
    //Calc cursor here,to make it work when monitor changed on runtime to retina
    QPixmap cursorPixmap = QPixmap( ":/RetinaIMG/RetinaIMG/Actions-color-picker-icon.png" )
                                   .scaled( 32 * devicePixelRatio(),
                                            32 * devicePixelRatio(),
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    cursorPixmap.setDevicePixelRatio( devicePixelRatio() );
    if( on ) viewport()->setCursor(QCursor(cursorPixmap,4,27));
    else viewport()->setCursor( Qt::OpenHandCursor );
}

//Set cross cursor active
void GraphicsZoomView::setCrossCursorActive(bool on)
{
    if( on ) viewport()->setCursor( Qt::CrossCursor );
    else viewport()->setCursor( Qt::OpenHandCursor );
}

//Methods for changing the cursor
void GraphicsZoomView::enterEvent(QEvent *event)
{
    QGraphicsView::enterEvent(event);
    //viewport()->setCursor(QCursor(m_cursorPixmap,0,31));
}

//Mouse was pressed
void GraphicsZoomView::mousePressEvent(QMouseEvent *event)
{
    QGraphicsView::mousePressEvent(event);
    m_isMousePressed = true;
    //viewport()->setCursor(QCursor(m_cursorPixmap,0,31));
    if( m_isWbPickerActive )
    {
        m_isWbPickerActive = false;
        emit wbPicked( event->pos().x(), event->pos().y() );
    }
}

//Mouse was released
void GraphicsZoomView::mouseReleaseEvent(QMouseEvent *event)
{
    m_isMousePressed = false;
    QGraphicsView::mouseReleaseEvent(event);
    //viewport()->setCursor(QCursor(m_cursorPixmap,0,31));
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
