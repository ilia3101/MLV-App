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
    static bool isPixelIncluded( mlvObject_t *pMlvObject, uint32_t x, uint32_t y );

private:
    static QString getFileName( mlvObject_t *pMlvObject );
    static uint32_t getRealX( mlvObject_t *pMlvObject, uint32_t x );
    static uint32_t getRealY( mlvObject_t *pMlvObject, uint32_t y );
    static QString getPixelLine( uint32_t x, uint32_t y );
};

#endif // BADPIXELFILEHANDLER_H
