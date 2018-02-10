/*!
 * \file ColorWheel.h
 * \author masc4ii
 * \copyright 2018
 * \brief A ColorWheel as QLabel
 */

#ifndef COLORWHEEL_H
#define COLORWHEEL_H

#include <QLabel>
#include <QPixmap>
#include <Qt>

class ColorWheel : public QLabel
{
    Q_OBJECT
public:
    ColorWheel(QWidget* parent = Q_NULLPTR);
    ~ColorWheel();
    void paintElement(void);

private:
    QImage *m_pImage;
    QPoint m_cursor;
    int16_t m_yaw;
    bool m_cursorSelected;
    bool m_wheelSelected;
    uint16_t m_size;

protected:
    void mousePressEvent(QMouseEvent* mouse);
    void mouseReleaseEvent(QMouseEvent*);
    void mouseMoveEvent(QMouseEvent* mouse);
};

#endif // COLORWHEEL_H
