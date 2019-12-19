/*!
 * \file BadPixelFileHandler.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Operations needed for bad pixel file handling
 */

#include "BadPixelFileHandler.h"
#include "MoveToTrash.h"
#include "stdint.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

//Constructor
BadPixelFileHandler::BadPixelFileHandler()
{

}

//Add a pixel to bpm file
void BadPixelFileHandler::addPixel(mlvObject_t *pMlvObject, uint32_t x, uint32_t y)
{
    QString fileName = getFileName( pMlvObject );
    uint32_t xCor = getRealX( pMlvObject, x );
    uint32_t yCor = getRealY( pMlvObject, y );
    //qDebug() << x << y << xCor << yCor << pMlvObject->VIDF.cropPosX << pMlvObject->VIDF.cropPosY;
    QFile file( fileName );
    file.open( QIODevice::WriteOnly | QIODevice::Append );
    QTextStream ts(&file);
    ts << getPixelLine( xCor, yCor );
    file.close();
}

//Delete a pixel from bpm file
void BadPixelFileHandler::removePixel(mlvObject_t *pMlvObject, uint32_t x, uint32_t y)
{
    QString fileName = getFileName( pMlvObject );
    uint32_t xCor = getRealX( pMlvObject, x );
    uint32_t yCor = getRealY( pMlvObject, y );

    QFile file( fileName );
    file.open( QIODevice::ReadOnly );
    QString txt = file.readAll();
    file.close();

    if( !txt.contains( getPixelLine( xCor, yCor ) ) ) return;
    txt.remove( getPixelLine( xCor, yCor ) );

    file.open( QIODevice::WriteOnly );
    QTextStream ts(&file);
    ts << txt;
    file.close();
}

//Delete the current bad pixel map
int BadPixelFileHandler::deleteCurrentMap(mlvObject_t *pMlvObject)
{
    QString fileName = getFileName( pMlvObject );
    if( QFileInfo( fileName ).exists() )
    {
        return MoveToTrash( QFileInfo( fileName ).absoluteFilePath() );
    }
    return 1;
}

//Is pixel included in the bpm file?
bool BadPixelFileHandler::isPixelIncluded(mlvObject_t *pMlvObject, uint32_t x, uint32_t y)
{
    QString fileName = getFileName( pMlvObject );
    if( !QFileInfo( fileName ).exists() ) return false;

    uint32_t xCor = getRealX( pMlvObject, x );
    uint32_t yCor = getRealY( pMlvObject, y );

    QFile file( fileName );
    file.open( QIODevice::ReadOnly );
    QString txt = file.readAll();
    file.close();

    return txt.contains( getPixelLine( xCor, yCor ) );
}

//Prepare all crosses in GraphicsScene, to be able to show it...
void BadPixelFileHandler::crossesPrepareAll(mlvObject_t *pMlvObject, QVector<CrossElement *> *pCrossVector, GraphicsPickerScene* pScene)
{
    for( int i = 0; i < pCrossVector->size(); i++ )
    {
        pCrossVector->at(i)->crossGraphicsElement()->hide();
        pScene->removeItem( pCrossVector->at(i)->crossGraphicsElement() );
    }
    pCrossVector->clear();

    QString fileName = getFileName( pMlvObject );
    if( !QFileInfo( fileName ).exists() ) return;

    QFile file( fileName );
    file.open( QIODevice::ReadOnly );
    QString txt = file.readAll();
    file.close();

    txt.replace( "\r", "" );
    txt.replace( "\t", "" );
    QStringList pixels = txt.split( "\n", QString::SkipEmptyParts );
    foreach( QString pixel, pixels )
    {
        bool ok;
        QStringList xy = pixel.split( " ", QString::SkipEmptyParts );
        uint32_t x = xy.at( 0 ).toUInt( &ok );
        if( !ok ) continue;
        uint32_t y = xy.at( 1 ).toUInt( &ok );
        if( !ok ) continue;
        x = getPicX( pMlvObject, x );
        y = getPicY( pMlvObject, y );
        //paintCross( pMlvObject, pRawImage, x, y );

        QPolygon poly;
        CrossElement *cross = new CrossElement( poly );
        cross->setPosition( x, y );
        pCrossVector->append( cross );
        pScene->addItem( pCrossVector->last()->crossGraphicsElement() );
    }
}

//Show all crosses in GraphicsScene
void BadPixelFileHandler::crossesShowAll(QVector<CrossElement *> *pCrossVector)
{
    for( int i = 0; i < pCrossVector->size(); i++ )
    {
        pCrossVector->at(i)->crossGraphicsElement()->show();
    }
}

//Hide all crosses in GraphicsScene
void BadPixelFileHandler::crossesHideAll(QVector<CrossElement *> *pCrossVector)
{
    for( int i = 0; i < pCrossVector->size(); i++ )
    {
        pCrossVector->at(i)->crossGraphicsElement()->hide();
    }
}

//Redraw all crosses into scene
void BadPixelFileHandler::crossesRedrawAll(mlvObject_t *pMlvObject, QVector<CrossElement *> *pCrossVector, GraphicsPickerScene *pScene)
{
    for( int i = 0; i < pCrossVector->size(); i++ )
    {
        pCrossVector->at(i)->redrawCrossElement( pScene->width(),
                                                 pScene->height(),
                                                 getMlvWidth( pMlvObject ),
                                                 getMlvHeight( pMlvObject ) );
    }
}

//How should be the bpm file be named?
QString BadPixelFileHandler::getFileName(mlvObject_t *pMlvObject)
{
    return QString( "%1_%2x%3.bpm" ).arg( pMlvObject->IDNT.cameraModel, 0, 16 )
                                    .arg( pMlvObject->RAWI.raw_info.width )
                                    .arg( pMlvObject->RAWI.raw_info.height );
}

//Calc the pixel x depending on raw buffer
uint32_t BadPixelFileHandler::getRealX(mlvObject_t *pMlvObject, uint32_t x)
{
    return x+pMlvObject->VIDF.cropPosX;
}

//Calc the pixel y depending on raw buffer
uint32_t BadPixelFileHandler::getRealY(mlvObject_t *pMlvObject, uint32_t y)
{
    return y+pMlvObject->VIDF.cropPosY;
}

//Calc the pixel x depending on raw buffer, inverse
uint32_t BadPixelFileHandler::getPicX(mlvObject_t *pMlvObject, uint32_t x)
{
    return x-pMlvObject->VIDF.cropPosX;
}

//Calc the pixel y depending on raw buffer, inverse
uint32_t BadPixelFileHandler::getPicY(mlvObject_t *pMlvObject, uint32_t y)
{
    return y-pMlvObject->VIDF.cropPosY;
}

//A line in the bpm file
QString BadPixelFileHandler::getPixelLine(uint32_t x, uint32_t y)
{
    return QString( "%1 %2\n" ).arg( x ).arg( y );
}

//Paint a cross
void BadPixelFileHandler::paintCross(mlvObject_t *pMlvObject, uint8_t *pRawImage, uint32_t x, uint32_t y)
{
    paintPixel( pMlvObject, pRawImage, x - 2, y, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x - 3, y, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x + 2, y, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x + 3, y, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x, y - 2, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x, y - 3, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x, y + 2, 255, 255, 255 );
    paintPixel( pMlvObject, pRawImage, x, y + 3, 255, 255, 255 );
}

//Paint a pixel
void BadPixelFileHandler::paintPixel(mlvObject_t *pMlvObject, uint8_t *pRawImage, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
    if( x >= getMlvWidth( pMlvObject )
     || y >= getMlvHeight( pMlvObject ) ) return;
    pRawImage[ ( y * getMlvWidth( pMlvObject ) + x ) * 3 + 0 ] = r;
    pRawImage[ ( y * getMlvWidth( pMlvObject ) + x ) * 3 + 1 ] = g;
    pRawImage[ ( y * getMlvWidth( pMlvObject ) + x ) * 3 + 2 ] = b;
}
