/*!
 * \file FocusPixelMapManager.h
 * \author masc4ii
 * \copyright 2020
 * \brief Check and install focus pixel maps
 */

#ifndef FOCUSPIXELMAPMANAGER_H
#define FOCUSPIXELMAPMANAGER_H

#include <QObject>
#include "../../src/mlv_include.h"
#include "DownloadManager.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

class FocusPixelMapManager : public QObject
{
    Q_OBJECT
public:
    explicit FocusPixelMapManager( QObject *parent = nullptr );
    ~FocusPixelMapManager();

    bool isDownloaded( mlvObject_t *pMlvObject );
    bool isMapAvailable( mlvObject_t *pMlvObject );
    bool downloadMap( mlvObject_t *pMlvObject );
    bool downloadAllMaps( mlvObject_t *pMlvObject );
    bool updateAllMaps( void );

private:
    DownloadManager *manager;
    QJsonArray getMapList( void );
    QString getMapName( mlvObject_t *pMlvObject );
};

#endif // FOCUSPIXELMAPMANAGER_H
