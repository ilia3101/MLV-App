/*!
 * \file CUpdater.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief This class checks in background if there is an update
 */

#include "CUpdater.h"

//Constructor
CUpdater::CUpdater(QObject *parent, const QString &githubRepoAddress, const QString &versionString) :
    QObject(parent),
    _updater(githubRepoAddress, versionString)
{

}

//Check for updates!
void CUpdater::checkForUpdates()
{
    _updater.setUpdateStatusListener(this);
    _updater.checkForUpdates();
}

//Send signal
void CUpdater::onUpdateAvailable(CAutoUpdaterGithub::ChangeLog changelog)
{
    emit updateAvailable( !changelog.empty() );
}
