/*!
 * \file GraphicsPickerScene.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A GraphicsScene with picker functionality
 */

#include "GraphicsPickerScene.h"

//Constructor
GraphicsPickerScene::GraphicsPickerScene(QObject *parent) :
    QGraphicsScene(parent)
{
    m_isWbPickerActive = false;
    m_isGradientAdjustment = false;
}

//Set picker on/off
void GraphicsPickerScene::setWbPickerActive(bool on)
{
    m_isWbPickerActive = on;
}

//Enable / disable Gradient adjustment
void GraphicsPickerScene::setGradientAdjustment(bool on)
{
    m_isGradientAdjustment = on;
}

//Click event
void GraphicsPickerScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mousePressEvent(event);
    if( m_isWbPickerActive )
    {
        m_isWbPickerActive = false;
        emit wbPicked( event->scenePos().x(), event->scenePos().y() );
    }
    if( m_isGradientAdjustment )
    {
        emit gradientAnchor( event->scenePos().x(), event->scenePos().y() );
    }
}

//Mouse release event
void GraphicsPickerScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseReleaseEvent(event);
    if( m_isGradientAdjustment )
    {
        emit gradientFinalPos( event->scenePos().x(), event->scenePos().y() );
    }
}
