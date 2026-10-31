/*!
 * \file NoScrollComboBox.h
 * \author masc4ii
 * \copyright 2019
 * \brief A Combobox which doesn't change item when scrolling
 */

#ifndef NOSCROLLCOMBOBOX_H
#define NOSCROLLCOMBOBOX_H

#include <QComboBox>
#include <QWidget>
#include <Qt>
#include <QWheelEvent>

class NoScrollComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit NoScrollComboBox(QWidget* parent = Q_NULLPTR);

protected:
    void wheelEvent(QWheelEvent *event) override;
};

#endif // NOSCROLLCOMBOBOX_H
