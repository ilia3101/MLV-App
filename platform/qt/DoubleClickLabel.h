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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event);
#else
    void enterEvent(QEvent *event);
#endif
    void leaveEvent(QEvent *event);
};

#endif // DOUBLECLICKLABEL_H
