/*!
 * \file GraphicsZoomView.h
 * \author masc4ii
 * \copyright 2017
 * \brief A QGraphicsView without scrolling but with zoom on mousewheel or y-axis on trackpad
 */

#ifndef GRAPHICSZOOMVIEW_H
#define GRAPHICSZOOMVIEW_H

#include <QGraphicsView>
#include <QWheelEvent>
#include <QWidget>
#include <Qt>
#include <QDebug>
#include <QShortcut>

class GraphicsZoomView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit GraphicsZoomView(QWidget *parent = 0);
    ~GraphicsZoomView();
    void setZoomEnabled(bool on);
    bool isZoomEnabled(void);
    void resetZoom(void);
    void setWbPickerActive(bool on);
    void setBpPickerActive(bool on);
    void setCrossCursorActive(bool on);

public slots:
    void shortCutZoomIn(void);
    void shortCutZoomOut(void);

signals:
    void wbPicked( int x, int y );
    void bpPicked( int x, int y );

protected:
    enum PickerState{ NoPicker, WbPicker, BpPicker };
    void enterEvent(QEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    void setPipetteCursor();
    bool m_isZoomEnabled;
    PickerState m_pickerState;
    bool m_isMousePressed;
    QShortcut *m_pZoomInSc;
    QShortcut *m_pZoomOutSc;
};

#endif // GRAPHICSZOOMVIEW_H
