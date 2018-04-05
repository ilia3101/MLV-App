#include "cautoupdatergithub.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProcess>
#include <QStringBuilder>

#if defined _WIN32
#define UPDATE_FILE_EXTENSION QLatin1String(".zip")
#elif defined __APPLE__
#define UPDATE_FILE_EXTENSION QLatin1String(".zip")
#else
#define UPDATE_FILE_EXTENSION QLatin1String(".zip")
#endif

static const auto naturalSortQstringComparator = [](const QString& l, const QString& r) {
	static QCollator collator;
	collator.setNumericMode(true);
	collator.setCaseSensitivity(Qt::CaseInsensitive);
	return collator.compare(l, r) == -1;
};

CAutoUpdaterGithub::CAutoUpdaterGithub(const QString& githubRepositoryAddress, const QString& currentVersionString, const std::function<bool (const QString&, const QString&)>& versionStringComparatorLessThan) :
	_updatePageAddress(githubRepositoryAddress + "/releases/"),
	_currentVersionString(currentVersionString),
	_lessThanVersionStringComparator(versionStringComparatorLessThan ? versionStringComparatorLessThan : naturalSortQstringComparator)
{
    //qDebug() << "githubRepositoryAddress.contains(https://github.com/)" << githubRepositoryAddress.contains("https://github.com/");
    //qDebug() << "!currentVersionString.isEmpty()" << !currentVersionString.isEmpty();
}

void CAutoUpdaterGithub::setUpdateStatusListener(UpdateStatusListener* listener)
{
	_listener = listener;
}

void CAutoUpdaterGithub::checkForUpdates()
{
	QNetworkReply * reply = _networkManager.get(QNetworkRequest(QUrl(_updatePageAddress)));
	if (!reply)
	{
		if (_listener)
			_listener->onUpdateError("Network request rejected.");
		return;
	}

	connect(reply, &QNetworkReply::finished, this, &CAutoUpdaterGithub::updateCheckRequestFinished, Qt::UniqueConnection);
}

void CAutoUpdaterGithub::downloadAndInstallUpdate(const QString& updateUrl)
{
	QNetworkReply * reply = _networkManager.get(QNetworkRequest(QUrl(updateUrl)));
	if (!reply)
	{
		if (_listener)
			_listener->onUpdateError("Network request rejected.");
		return;
	}

	connect(reply, &QNetworkReply::downloadProgress, this, &CAutoUpdaterGithub::onDownloadProgress);
	connect(reply, &QNetworkReply::finished, this, &CAutoUpdaterGithub::updateDownloaded, Qt::UniqueConnection);
}

inline QString match(const QString& pattern, const QString& text, int from, int& end)
{
	end = -1;

	const auto delimiters = pattern.split('*');
    //assert_and_return_message_r(delimiters.size() == 2, "Invalid pattern", QString());
    if( delimiters.size() != 2 )
    {
        qDebug() << "invalid pattern" << pattern;
        return QString();
    }

	const int leftDelimiterStart = text.indexOf(delimiters[0], from);
	if (leftDelimiterStart < 0)
		return QString();

	const int rightDelimiterStart = text.indexOf(delimiters[1], leftDelimiterStart + delimiters[0].length());
	if (rightDelimiterStart < 0)
		return QString();

	const int resultLength = rightDelimiterStart - leftDelimiterStart - delimiters[0].length();
	if (resultLength <= 0)
		return QString();

	end = rightDelimiterStart + delimiters[1].length();

    //qDebug() << "A VERSION:" << pattern << text.mid(leftDelimiterStart + delimiters[0].length(), resultLength);

	return text.mid(leftDelimiterStart + delimiters[0].length(), resultLength);
}

void CAutoUpdaterGithub::updateCheckRequestFinished()
{
	QNetworkReply* reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;

	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError)
	{
		if (_listener)
			_listener->onUpdateError(reply->errorString());

		return;
	}

	if (reply->bytesAvailable() <= 0)
	{
		if (_listener)
			_listener->onUpdateError("No data downloaded.");
		return;
	}

	ChangeLog changelog;
	static const QString changelogPattern = "<div class=\"markdown-body\">\n*</div>";
	static const QString versionPattern = "/releases/tag/*\">";
	static const QString releaseUrlPattern = "<a href=\"*\"";
	
	const auto releases = QString(reply->readAll()).split("release-header");
	// Skipping the 0 item because anything before the first "release-header" is not a release
	for (int releaseIndex = 1, numItems = releases.size(); releaseIndex < numItems; ++releaseIndex)
	{
		const QString& releaseText = releases[releaseIndex];
	
		int offset = 0;
		const QString updateVersion = match(versionPattern, releaseText, offset, offset);
		if (!naturalSortQstringComparator(_currentVersionString, updateVersion))
			continue; // version <= _currentVersionString, skipping

        if (!updateVersion.startsWith( "QT" ))
            continue;

        const QString updateChanges = match(changelogPattern, releaseText, offset, offset);

        QString url = QString( "/ilia3101/MLV-App/releases" );
        while (offset != -1)
		{

			const QString newUrl = match(releaseUrlPattern, releaseText, offset, offset);
			if (newUrl.endsWith(UPDATE_FILE_EXTENSION))
			{
                //assert_message_r(url.isEmpty(), "More than one suitable update URL found");
                if( !url.isEmpty() ) qDebug() << "More than one suitable update URL found";
				url = newUrl;
			}
        }

		if (!url.isEmpty())
			changelog.push_back({ updateVersion, updateChanges, "https://github.com" + url });
	}

	if (_listener)
		_listener->onUpdateAvailable(changelog);
}

void CAutoUpdaterGithub::updateDownloaded()
{
	QNetworkReply* reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;

	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError)
	{
		if (_listener)
			_listener->onUpdateError(reply->errorString());

		return;
	}

	const QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
	if (!redirectUrl.isEmpty())
	{
		// We are being redirected
		reply = _networkManager.get(QNetworkRequest(redirectUrl));
		if (!reply)
		{
			if (_listener)
				_listener->onUpdateError("Network request rejected.");
			return;
		}

		connect(reply, &QNetworkReply::downloadProgress, this, &CAutoUpdaterGithub::onDownloadProgress);
		connect(reply, &QNetworkReply::finished, this, &CAutoUpdaterGithub::updateDownloaded, Qt::UniqueConnection);

		return;
	}

	if (_listener)
		_listener->onUpdateDownloadFinished();

	if (reply->bytesAvailable() <= 0)
	{
		if (_listener)
			_listener->onUpdateError("No data downloaded.");
		return;
	}

	QFile tempExeFile(QDir::tempPath() % '/' % QCoreApplication::applicationName() % ".exe");
	if (!tempExeFile.open(QFile::WriteOnly))
	{
		if (_listener)
			_listener->onUpdateError("Failed to open temporary file.");
		return;
	}
	tempExeFile.write(reply->readAll());
	tempExeFile.close();

	if (!QProcess::startDetached('\"' % tempExeFile.fileName() % '\"') && _listener)
		_listener->onUpdateError("Failed to launch the downloaded update.");
}

void CAutoUpdaterGithub::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
	if (_listener)
		_listener->onUpdateDownloadProgress(bytesReceived < bytesTotal ? bytesReceived * 100 / (float)bytesTotal : 100.0f);
}
