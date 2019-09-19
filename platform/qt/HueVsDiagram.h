/*!
 * \file HueVsDiagram.h
 * \author masc4ii
 * \copyright 2018
 * \brief The gradation HueVsSat element
 */

#ifndef HUEVSDIAGRAM_H
#define HUEVSDIAGRAM_H

#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <Qt>
#include "../../src/mlv_include.h"

class HueVsDiagram : public QLabel
{
    Q_OBJECT
public:
    HueVsDiagram(QWidget* parent = Q_NULLPTR);
    ~HueVsDiagram();
    void setProcessingObject( processingObject_t *processing );
    void paintElement(void);
    void setFrameChangedPointer( bool *pFrameChanged );
    void resetLine( void );
    void setConfiguration(QString config );
    QString configuration( void );
    enum DiagramType{ HueVsHue, HueVsSaturation, HueVsLuminance, LuminanceVsSaturation };
    void setDiagramType( DiagramType type );

private:
    QImage *m_pImage;
    QPoint m_cursor;
    uint16_t m_width;
    bool m_pointSelected;
    bool m_firstPoint;
    bool m_lastPoint;
    bool *m_pFrameChanged;
    bool m_LoadAll;
    DiagramType m_diagramType;
    uint8_t m_activeLine;
    uint16_t m_size;
    QVector<QPointF> m_whiteLine;
    processingObject_t *m_pProcessing;

protected:
    void mousePressEvent(QMouseEvent* mouse);
    void mouseDoubleClickEvent(QMouseEvent* mouse);
    void mouseReleaseEvent(QMouseEvent*);
    void mouseMoveEvent(QMouseEvent* mouse);
    void paintLine(QVector<QPointF> line , QPainter *pPainter, QColor color, bool active);
    void initLine(QVector<QPointF> *line);
    void movePoint(qreal x, qreal y, bool release );
    void resizeEvent( QResizeEvent *event );
};

#endif // HUEVSDIAGRAM_H
