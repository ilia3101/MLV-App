#ifndef GRAPHICSZOOMVIEW_H
#define GRAPHICSZOOMVIEW_H

#include <QGraphicsView>
#include <QWheelEvent>
#include <QWidget>
#include <Qt>
#include <QDebug>

class GraphicsZoomView : public QGraphicsView
{
public:
    explicit GraphicsZoomView(QWidget* parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());

protected:
    void enterEvent(QEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
};

#endif // GRAPHICSZOOMVIEW_H
