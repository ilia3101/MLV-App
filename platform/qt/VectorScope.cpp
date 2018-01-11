/*!
 * \file VectorScope.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Draws a RGB VectorScope for an image
 */

#include "VectorScope.h"
#include "math.h"

//Transformation
#define CALC_POINT_X( r, g, b ) ( ( -0.299*r - 0.587*g + 0.886*b ) * 0.493 * 0.4 ) //0.4 is scaling to 80% * retina
#define CALC_POINT_Y( r, g, b ) ( (  0.701*r - 0.587*g - 0.114*b ) * 0.877 * 0.4 ) //0.4 is scaling to 80% * retina

//Constructor
VectorScope::VectorScope( uint16_t widthScope, uint16_t heightScope )
{
    m_pScope = new QImage( widthScope, heightScope, QImage::Format_RGB888 );
    m_middleX = widthScope / 2.0;
    m_middleY = heightScope / 2.0;
}

//Destructor
VectorScope::~VectorScope()
{
    delete m_pScope;
}

//Calc VectorScope
QImage VectorScope::getVectorScopeFromRaw(uint8_t *m_pRawImage, uint16_t width, uint16_t height)
{
    m_pScope->fill( Qt::black );

    //ScopeCloud, each 4th row & column for speed reasons
    for( int x = 0; x < width; x += 4 )
    {
        for( int y = 0; y < height; y += 4 )
        {
            uint8_t r = m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 0 ];
            uint8_t g = m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 1 ];
            uint8_t b = m_pRawImage[ ( ( x + ( width * y ) ) * 3 ) + 2 ];

            int pointX = CALC_POINT_X( r, g, b );
            int pointY = CALC_POINT_Y( r, g, b );

            //make it lighter - blue is better visible then
            r = ( r + 200 ) / 2.0;
            g = ( g + 200 ) / 2.0;
            b = ( b + 200 ) / 2.0;

            //Show only the point with highest lightness
            if( m_pScope->pixelColor( pointX+m_middleX, -pointY+m_middleY ).lightness() < QColor( r, g, b, 255 ).lightness() )
            {
                m_pScope->setPixelColor( pointX+m_middleX, -pointY+m_middleY, QColor( r, g, b, 255 ) );
            }
        }
    }

    paintLines();

    paintCross( CALC_POINT_X( 255, 0, 0 ), CALC_POINT_Y( 255, 0, 0 ) );
    paintCross( CALC_POINT_X( 255, 255, 0 ), CALC_POINT_Y( 255, 255, 0 ) );
    paintCross( CALC_POINT_X( 0, 255, 0 ), CALC_POINT_Y( 0, 255, 0 ) );
    paintCross( CALC_POINT_X( 0, 255, 255 ), CALC_POINT_Y( 0, 255, 255 ) );
    paintCross( CALC_POINT_X( 0, 0, 255 ), CALC_POINT_Y( 0, 0, 255 ) );
    paintCross( CALC_POINT_X( 255, 0, 255 ), CALC_POINT_Y( 255, 0, 255 ) );

    return *m_pScope;
}

//Paint lines into scope
void VectorScope::paintLines()
{
    //Lines
    for( int y = 0; y < m_pScope->size().height(); y++ )
    {
        //line vert
        m_pScope->setPixelColor( m_middleX, y, Qt::darkGray );

        //line hori
        m_pScope->setPixelColor( y + m_middleX - m_middleY, m_middleY, Qt::darkGray );
    }

    //Circle
    int circle_radius = m_middleY;
    for (int i = 0; i <= 2 * circle_radius; i++)
    {
        for (int j = 0; j < 2 * circle_radius; j++)
        {
            float distance_to_centre = sqrt((i - circle_radius)*(i - circle_radius) + (j - circle_radius)*(j - circle_radius));
            if (distance_to_centre > circle_radius-1.5 && distance_to_centre < circle_radius+0.5)
            {
                 m_pScope->setPixelColor( i + m_middleX - m_middleY, j, Qt::darkGray );
            }
        }
    }
}

//Paint a cross at x,y
void VectorScope::paintCross(int x, int y)
{
    for( int i = -4; i < 5; i++ )
    {
        m_pScope->setPixelColor( x + i + m_middleX, -y + m_middleY, Qt::darkGray );
        m_pScope->setPixelColor( x + m_middleX, -y + i + m_middleY, Qt::darkGray );
    }
}
