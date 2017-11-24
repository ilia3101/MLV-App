/*!
 * \file GraphicsPolygonMoveItem.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief A GraphicsPolygonItem which tells when it was moved
 */

#include "GraphicsPolygonMoveItem.h"
#include <QDebug>

GraphicsPolygonMoveItem::GraphicsPolygonMoveItem(QPolygon polygon, QGraphicsItem *parent) : QGraphicsPolygonItem(polygon,parent)
{
}

//Someone is dragging the polygon
QVariant GraphicsPolygonMoveItem::itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant &value)
{
    // value seems to contain position relative start of moving
    if( change == ItemPositionChange )
    {
        //qDebug() << value.toPoint().x() << value.toPoint().y();
        emit itemMoved( value.toPoint().x(), value.toPoint().y() );
    }
    return QGraphicsPolygonItem::itemChange(change, value); // i also tried to call this before the emiting
}
