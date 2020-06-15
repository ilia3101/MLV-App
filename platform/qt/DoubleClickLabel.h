/*!
 * \file DoubleClickLabel.h
 * \author masc4ii
 * \copyright 2017
 * \brief A doubleclickable QLabel
 */

#ifndef DOUBLECLICKLABEL_H
#define DOUBLECLICKLABEL_H

#include <QLabel>
#include <QWidget>
#include <Qt>

class DoubleClickLabel : public QLabel
{
    Q_OBJECT
public:
    explicit DoubleClickLabel(QWidget* parent = Q_NULLPTR);
    ~DoubleClickLabel();

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent*);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);
};

#endif // DOUBLECLICKLABEL_H
