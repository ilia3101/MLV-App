/*!
 * \file FocusPixelMapManager.cpp
 * \author masc4ii
 * \copyright 2020
 * \brief Check and install focus pixel maps
 */

#include "FocusPixelMapManager.h"
#include <QMessageBox>

//Constructor
FocusPixelMapManager::FocusPixelMapManager(QObject *parent) : QObject(parent)
{
    manager = new DownloadManager();
}

//Destructor
FocusPixelMapManager::~FocusPixelMapManager()
{
    delete manager;
}

bool FocusPixelMapManager::isDownloaded(mlvObject_t *pMlvObject)
{
    QString searchName = getMapName( pMlvObject );
    QString fileName = QString( "%1/%2" ).arg( QCoreApplication::applicationDirPath() ).arg( searchName );
    return QFileInfo( fileName ).exists();
}

//Check for fpm in repos online
bool FocusPixelMapManager::isMapAvailable(mlvObject_t *pMlvObject)
{
    QString searchName = getMapName( pMlvObject );
    QJsonArray files = getMapList();
    if( files.empty() )
    {
        return false;
    }
    foreach( QJsonValue entry, files )
    {
        if( entry.toObject().value( "name" ).toString() == searchName ) return true;
    }
    return false;
}

//Download and install fpm from repos in application
bool FocusPixelMapManager::downloadMap(mlvObject_t *pMlvObject)
{
    QString searchName = getMapName( pMlvObject );
    QJsonArray files = getMapList();
    if( files.empty() )
    {
        return false;
    }
    foreach( QJsonValue entry, files )
    {
        if( entry.toObject().value( "name" ).toString() == searchName )
        {
            manager->doDownload( QUrl( entry.toObject().value( "download_url" ).toString() ) );
            while( !manager->isDownloadReady() )
            {
                qApp->processEvents();
            }
            return manager->downloadSuccess();
        }
    }
    return false;
}

//Download and install all fpm for current camera from repos in application
bool FocusPixelMapManager::downloadAllMaps(mlvObject_t *pMlvObject)
{
    bool installed = false;
    QString searchName = QString( "%1" ).arg( pMlvObject->IDNT.cameraModel, 0, 16 );
    QJsonArray files = getMapList();
    if( files.empty() )
    {
        return false;
    }
    foreach( QJsonValue entry, files )
    {
        if( entry.toObject().value( "name" ).toString().startsWith( searchName ) )
        {
            manager->doDownload( QUrl( entry.toObject().value( "download_url" ).toString() ) );
            while( !manager->isDownloadReady() )
            {
                qApp->processEvents();
            }
            if( manager->downloadSuccess() ) installed = true;
            else return false;
        }
    }

    return installed;
}

//Get map list online from repos
QJsonArray FocusPixelMapManager::getMapList()
{
    manager->doDownload( QUrl( "https://api.github.com/repos/ilia3101/MLV-App/contents/pixel_maps" ) );

    while( !manager->isDownloadReady() )
    {
        qApp->processEvents();
    }

    QString fileName = QString( "%1/pixel_maps" ).arg( QCoreApplication::applicationDirPath() );
    QFile file( fileName );
    if( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    {
        qDebug() << "open paths json file failed.";
        QJsonArray a;
        return a;
    }
    QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    file.close();

    /* JSON is invalid */
    if (doc.isNull()) {
        qDebug() << "paths json file invalid.";
        QJsonArray a;
        return a;
    }

    return doc.array();
}

//Get the map name for the current clip
QString FocusPixelMapManager::getMapName(mlvObject_t *pMlvObject)
{
    QString searchName = QString( "%1_%2x%3.fpm" ).arg( pMlvObject->IDNT.cameraModel, 0, 16 )
            .arg( pMlvObject->RAWI.raw_info.width )
            .arg( pMlvObject->RAWI.raw_info.height );

    return searchName;
}
