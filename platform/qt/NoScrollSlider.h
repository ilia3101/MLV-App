/*!
 * \file NoScrollSlider.h
 * \author masc4ii
 * \copyright 2017
 * \brief A slider which can't scroll
 */

#ifndef NOSCROLLSLIDER_H
#define NOSCROLLSLIDER_H

#include <QSlider>
#include <QWidget>
#include <Qt>

class NoScrollSlider : public QSlider
{
    Q_OBJECT
public:
    explicit NoScrollSlider(QWidget* parent = Q_NULLPTR);

signals:
    void doubleClicked();

protected:
    void wheelEvent(QWheelEvent*);
    void mouseDoubleClickEvent(QMouseEvent*);
};

#endif // NOSCROLLSLIDER_H
