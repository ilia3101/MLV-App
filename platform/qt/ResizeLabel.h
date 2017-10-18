/*!
 * \file ResizeLabel.h
 * \author masc4ii
 * \copyright 2017
 * \brief A QLabel which is resizeable
 */

#ifndef RESIZELABEL_H
#define RESIZELABEL_H

#include <QLabel>
#include <QWidget>
#include <Qt>

class ResizeLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ResizeLabel(QWidget* parent = Q_NULLPTR);
    ~ResizeLabel();

signals:
    void sizeChanged();

protected:
    void resizeEvent(QResizeEvent *);
};

#endif // RESIZELABEL_H
