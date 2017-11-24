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

class GraphicsPolygonMoveItem : public QObject, public QGraphicsPolygonItem
{
    Q_OBJECT
public:
    explicit GraphicsPolygonMoveItem(QPolygon polygon, QGraphicsItem *parent = 0);

private:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value);

signals:
    void itemMoved( int x, int y );
};

#endif // GRAPHICSPOLYGONMOVEITEM_H
