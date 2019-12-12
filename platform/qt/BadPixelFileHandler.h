/*!
 * \file BadPixelFileHandler.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Operations needed for bad pixel file handling
 */

#ifndef BADPIXELFILEHANDLER_H
#define BADPIXELFILEHANDLER_H

#include "../../src/mlv_include.h"
#include <QString>

class BadPixelFileHandler
{
public:
    BadPixelFileHandler();
    static void addPixel( mlvObject_t *pMlvObject, uint32_t x, uint32_t y );
    static void removePixel( mlvObject_t *pMlvObject, uint32_t x, uint32_t y );
    static void deleteCurrentMap( mlvObject_t *pMlvObject );
    static bool isPixelIncluded( mlvObject_t *pMlvObject, uint32_t x, uint32_t y );
    static void drawBadPixels( mlvObject_t *pMlvObject, uint8_t *pRawImage );

private:
    static QString getFileName( mlvObject_t *pMlvObject );
    static uint32_t getRealX( mlvObject_t *pMlvObject, uint32_t x );
    static uint32_t getRealY( mlvObject_t *pMlvObject, uint32_t y );
    static uint32_t getPicX( mlvObject_t *pMlvObject, uint32_t x );
    static uint32_t getPicY( mlvObject_t *pMlvObject, uint32_t y );
    static QString getPixelLine( uint32_t x, uint32_t y );
    static void paintCross( mlvObject_t *pMlvObject, uint8_t *pRawImage, uint32_t x, uint32_t y );
    static void paintPixel( mlvObject_t *pMlvObject, uint8_t *pRawImage, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b );
};

#endif // BADPIXELFILEHANDLER_H
