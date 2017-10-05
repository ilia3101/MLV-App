/*!
 * \file GraphicsPickerScene.h
 * \author masc4ii
 * \copyright 2017
 * \brief A GraphicsScene with picker functionality
 */

#ifndef GRAPHICSPICKERSCENE_H
#define GRAPHICSPICKERSCENE_H

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QObject>
#include <Qt>

class GraphicsPickerScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit GraphicsPickerScene(QObject *parent = 0);
    void setWbPickerActive(bool on);

signals:
    void wbPicked(int x, int y);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    bool m_isWbPickerActive;
};

#endif // GRAPHICSPICKERSCENE_H
