/*!
 * \file Updater.cpp
 * \author masc4ii
 * \copyright 2022
 * \brief Check for new MLVApp releases via github JSON API
 */

#include "Updater.h"
#include <QByteArray>
#include <QCollator>

static const auto naturalSortQstringComparator = [](const QString& l, const QString& r) {
    static QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(l, r) == -1;
};

Updater::Updater(QObject *parent, QUrl url, QString version)
    : QObject{parent}
{
    m_manager = new DownloadManager();
    m_url = url;
    m_version = version;
}

Updater::~Updater()
{
    delete m_manager;
}

bool Updater::isUpdateAvailable( void )
{
    QJsonArray releases = getReleaseList();
    if( releases.empty() )
    {
        return false;
    }
    //qDebug() << "current:" << version;

    m_changelog.clear();
    bool available = false;

    foreach( QJsonValue entry, releases )
    {
        QString releaseTag = entry.toObject().value( "tag_name" ).toString();
        /*qDebug() << "entry:"
                 << releaseTag
                 << naturalSortQstringComparator( version, releaseTag )
                 << releaseTag.startsWith( version.at(0) );*/

        if( !naturalSortQstringComparator( m_version, releaseTag ) ) continue;
        if( !releaseTag.startsWith( m_version.at(0) ) ) continue;
        available = true;
        m_changelog.push_back({ releaseTag,
                              entry.toObject().value( "body" ).toString(),
                              m_url.toString() });
    }
    return available;
}

QJsonArray Updater::getReleaseList( void )
{
    m_manager->doDownload( m_url );

    while( !m_manager->isDownloadReady() )
    {
        qApp->processEvents();
    }

    QString fileName = QString( "%1/releases" ).arg( QCoreApplication::applicationDirPath() );
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
