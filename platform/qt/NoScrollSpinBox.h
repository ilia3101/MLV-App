/*!
 * \file NoScrollSpinBox.h
 * \author masc4ii
 * \copyright 2019
 * \brief A Spinbox which doesn't change item when scrolling
 */

#ifndef NOSCROLLSPINBOX_H
#define NOSCROLLSPINBOX_H

#include <QSpinBox>
#include <QWidget>
#include <Qt>

class NoScrollSpinBox : public QSpinBox
{
    Q_OBJECT
public:
    explicit NoScrollSpinBox(QWidget* parent = Q_NULLPTR);

protected:
    void wheelEvent(QWheelEvent*);
};

#endif // NOSCROLLSPINBOX_H
