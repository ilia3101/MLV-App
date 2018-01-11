/*!
 * \file VectorScope.h
 * \author masc4ii
 * \copyright 2018
 * \brief Draws a RGB VectorScope for an image
 */

#ifndef VECTORSCOPE_H
#define VECTORSCOPE_H

#include <QImage>
#include <stdint.h>

class VectorScope
{
public:
    VectorScope(uint16_t widthScope, uint16_t heightScope);
    ~VectorScope();
    QImage getVectorScopeFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height);

private:
    QImage *m_pScope;
    int m_middleX;
    int m_middleY;

    void paintLines( void );
    void paintCross( int x, int y );
};

#endif // VECTORSCOPE_H
