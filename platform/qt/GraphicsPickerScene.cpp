/*!
 * \file GraphicsPickerScene.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A GraphicsScene with picker functionality
 */

#include "GraphicsPickerScene.h"
#include <QDebug>
#include <QMimeData>
#include <QUrl>

//Constructor
GraphicsPickerScene::GraphicsPickerScene(QObject *parent) :
    QGraphicsScene(parent)
{
    m_isWbPickerActive = false;
    m_isGradientAdjustment = false;
    m_isMousePressed = false;
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
    m_isMousePressed = false;
}

//Click event
void GraphicsPickerScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mousePressEvent(event);
    m_isMousePressed = true;
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
    m_isMousePressed = false;
    if( m_isGradientAdjustment )
    {
        emit gradientFinalPos( event->scenePos().x(), event->scenePos().y(), true );
    }
}

//Mouse move event
void GraphicsPickerScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseMoveEvent(event);
    if( m_isGradientAdjustment && m_isMousePressed )
    {
        emit gradientFinalPos( event->scenePos().x(), event->scenePos().y(), false );
    }
}

//Drop Event for opening MLV files
void GraphicsPickerScene::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    QStringList list;
    for( int i = 0; i < event->mimeData()->urls().count(); i++ )
    {
        list.append( event->mimeData()->urls().at(i).path() );
    }
    emit filesDropped( list );
    event->acceptProposedAction();
}
