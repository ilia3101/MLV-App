/*!
 * \file ColorToolButton.h
 * \author masc4ii
 * \copyright 2017
 * \brief A QToolbutton which changes the color when checked
 */

#ifndef COLORTOOLBUTTON_H
#define COLORTOOLBUTTON_H

#include <QToolButton>
#include <Qt>

class ColorToolButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorToolButton( QWidget* parent = Q_NULLPTR );

private slots:
    void buttonChecked(bool);

private:
    QPalette m_palette;
};

#endif // COLORTOOLBUTTON_H
