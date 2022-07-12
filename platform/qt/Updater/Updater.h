/*!
 * \file Updater.h
 * \author masc4ii
 * \copyright 2022
 * \brief Check for new MLVApp releases via github JSON API
 */

#ifndef UPDATER_H
#define UPDATER_H

#include <QObject>
#include "../DownloadManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

class Updater : public QObject
{
    Q_OBJECT
public:
    struct VersionEntry {
        QString versionString;
        QString versionChanges;
        QString versionUpdateUrl;
    };
    typedef std::vector<VersionEntry> ChangeLog;

    explicit Updater( QObject *parent = nullptr, QUrl url = QUrl(), QString version = "" );
    ~Updater();

    bool isUpdateAvailable( void );
    ChangeLog getUpdateChangelog( void ){ return m_changelog; }

private:
    DownloadManager *m_manager;
    QJsonArray getReleaseList( void );
    ChangeLog m_changelog;
    QUrl m_url;
    QString m_version;
};

#endif // UPDATER_H
