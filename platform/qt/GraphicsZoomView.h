/*!
 * \file GraphicsZoomView.h
 * \author masc4ii
 * \copyright 2017
 * \brief A QGraphicsView wihtout scrolling but with zoom on mousewheel or y-axis on trackpad
 */

#ifndef GRAPHICSZOOMVIEW_H
#define GRAPHICSZOOMVIEW_H

#include <QGraphicsView>
#include <QWheelEvent>
#include <QWidget>
#include <Qt>
#include <QDebug>
#include <QPixmap>

class GraphicsZoomView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit GraphicsZoomView(QWidget *parent = 0);
    void setZoomEnabled(bool on);
    void resetZoom(void);
    void setWbPickerActive(bool on);

signals:
    void wbPicked( int x, int y );

protected:
    void enterEvent(QEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    bool m_isZoomEnabled;
    bool m_isWbPickerActive;
    QPixmap m_cursorPixmap;
};

#endif // GRAPHICSZOOMVIEW_H
