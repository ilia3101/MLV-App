/*!
 * \file Curves.h
 * \author masc4ii
 * \copyright 2018
 * \brief The gradation curves element
 */

#ifndef CURVES_H
#define CURVES_H

#include <QLabel>
#include <QPixmap>
#include <Qt>
#include "../../src/mlv_include.h"

#define LINENR_W    0
#define LINENR_R    1
#define LINENR_G    2
#define LINENR_B    3

class Curves : public QLabel
{
    Q_OBJECT
public:
    Curves(QWidget* parent = Q_NULLPTR);
    ~Curves();
    void setProcessingObject( processingObject_t *processing );
    void paintElement(void);
    void setActiveLine( uint8_t lineNr );
    void setFrameChangedPointer( bool *pFrameChanged );
    void resetLines( void );
    void setConfiguration(QString config );
    QString configuration( void );

private:
    QImage *m_pImage;
    QPoint m_cursor;
    bool m_pointSelected;
    bool m_firstPoint;
    bool m_lastPoint;
    bool *m_pFrameChanged;
    bool m_LoadAll;
    uint8_t m_activeLine;
    uint16_t m_size;
    QVector<QPointF> m_whiteLine;
    QVector<QPointF> m_redLine;
    QVector<QPointF> m_greenLine;
    QVector<QPointF> m_blueLine;
    processingObject_t *m_pProcessing;

protected:
    void mousePressEvent(QMouseEvent* mouse);
    void mouseReleaseEvent(QMouseEvent*);
    void mouseMoveEvent(QMouseEvent* mouse);
    void paintLine(QVector<QPointF> line , QPainter *pPainter, QColor color, bool active, uint8_t channel);
    void initLine(QVector<QPointF> *line);
    void movePoint(qreal x, qreal y, bool release );
    QVector<QPointF> * getActiveLinePointer();
};

#endif // CURVES_H
