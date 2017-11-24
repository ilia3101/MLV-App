/*!
 * \file GraphicsPolygonMoveItem.h
 * \author masc4ii
 * \copyright 2017
 * \brief A GraphicsPolygonItem which tells when it was moved
 */

#ifndef GRAPHICSPOLYGONMOVEITEM_H
#define GRAPHICSPOLYGONMOVEITEM_H

#include <QObject>
#include <QGraphicsPolygonItem>
#include <QGraphicsSceneHoverEvent>

class GraphicsPolygonMoveItem : public QObject, public QGraphicsPolygonItem
{
    Q_OBJECT
public:
    explicit GraphicsPolygonMoveItem(QPolygon polygon, QGraphicsItem *parent = 0);

private:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value);
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event);
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event);

signals:
    void itemMoved( int x, int y );
    void itemHovered( bool isHovered );
};

#endif // GRAPHICSPOLYGONMOVEITEM_H
