#ifndef CUPDATER_H
/*!
 * \file CUpdater.h
 * \author masc4ii
 * \copyright 2018
 * \brief This class checks in background if there is an update
 */

#define CUPDATER_H

#include "../cautoupdatergithub.h"
#include <QObject>

class CAutoUpdaterGithub;

class CUpdater : public QObject, private CAutoUpdaterGithub::UpdateStatusListener
{
    Q_OBJECT
public:
    explicit CUpdater( QObject *parent = Q_NULLPTR, const QString& githubRepoAddress = "", const QString& versionString = "" );
    void checkForUpdates( void );

signals:
    void updateAvailable( bool );

private:
    // If no updates are found, the changelog is empty
    void onUpdateAvailable(CAutoUpdaterGithub::ChangeLog changelog) override;
    void onUpdateDownloadProgress(float percentageDownloaded) override {Q_UNUSED(percentageDownloaded);};
    void onUpdateDownloadFinished() override {};
    void onUpdateError(QString errorMessage) override {Q_UNUSED(errorMessage);};

    QString _latestUpdateUrl;
    CAutoUpdaterGithub _updater;
};

#endif // CUPDATER_H
