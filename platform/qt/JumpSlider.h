/*!
 * \file JumpSlider.h
 * \author masc4ii
 * \copyright 2017
 * \brief A slider which directly jumps to a clicked position
 */

#ifndef JUMPSLIDER_H
#define JUMPSLIDER_H

#include <QSlider>
#include <QWidget>
#include <Qt>
#include <QMouseEvent>

class JumpSlider : public QSlider
{
public:
    explicit JumpSlider( QWidget* parent = Q_NULLPTR ): QSlider( parent ) {}

protected:
    void mousePressEvent( QMouseEvent * event );
};

#endif // JUMPSLIDER_H
