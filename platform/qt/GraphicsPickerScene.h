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
#include <QGraphicsSceneDragDropEvent>
#include <QObject>
#include <Qt>

class GraphicsPickerScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit GraphicsPickerScene(QObject *parent = 0);
    void setWbPickerActive(bool on);
    void setGradientAdjustment(bool on);

signals:
    void wbPicked(int x, int y);
    void gradientAnchor(int x, int y);
    void gradientFinalPos(int x, int y, bool finished);
    void filesDropped(QStringList list);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event);
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event){(void)event;};
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event){(void)event;};
    void dragLeaveEvent(QGraphicsSceneDragDropEvent* event){(void)event;};
    void dropEvent(QGraphicsSceneDragDropEvent* event);
    bool m_isWbPickerActive;
    bool m_isGradientAdjustment;
    bool m_isMousePressed;
};

#endif // GRAPHICSPICKERSCENE_H
