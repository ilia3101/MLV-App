#include "cupdaterdialog.h"

#include "ui_cupdaterdialog.h"

#include <QDebug>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>
#include <QScrollBar>

CUpdaterDialog::CUpdaterDialog(QWidget *parent, const QString& githubRepoAddress, const QString& versionString, bool silentCheck) :
	QDialog(parent),
	ui(new Ui::CUpdaterDialog),
	_silent(silentCheck),
	_updater(githubRepoAddress, versionString)
{
	ui->setupUi(this);

	if (_silent)
		hide();

	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CUpdaterDialog::applyUpdate);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText("Download");

	ui->stackedWidget->setCurrentIndex(0);
	ui->progressBar->setMaximum(0);
	ui->progressBar->setValue(0);
	ui->lblPercentage->setVisible(false);

	_updater.setUpdateStatusListener(this);
	_updater.checkForUpdates();
}

CUpdaterDialog::~CUpdaterDialog()
{
	delete ui;
}

void CUpdaterDialog::applyUpdate()
{
    QDesktopServices::openUrl(QUrl(_latestUpdateUrl));
}

// If no updates are found, the changelog is empty
void CUpdaterDialog::onUpdateAvailable(CAutoUpdaterGithub::ChangeLog changelog)
{
	if (!changelog.empty())
	{
		ui->stackedWidget->setCurrentIndex(1);
		for (const auto& changelogItem: changelog)
			ui->changeLogViewer->append("<b>" % changelogItem.versionString % "</b>" % '\n' % changelogItem.versionChanges % "<p></p>");

		_latestUpdateUrl = changelog.front().versionUpdateUrl;
        QScrollBar *scrollbar = ui->changeLogViewer->verticalScrollBar();
        scrollbar->setSliderPosition(0);
		show();
	}
	else
	{
		accept();
		if (!_silent)
            QMessageBox::information(this, tr("No update available"), tr("You already have the latest version of MLV App."));
	}
}

// percentageDownloaded >= 100.0f means the download has finished
void CUpdaterDialog::onUpdateDownloadProgress(float percentageDownloaded)
{
	ui->progressBar->setValue((int)percentageDownloaded);
	ui->lblPercentage->setText(QString::number(percentageDownloaded, 'f', 2) + " %");
}

void CUpdaterDialog::onUpdateDownloadFinished()
{
	accept();
}

void CUpdaterDialog::onUpdateError(QString errorMessage)
{
	reject();
	if (!_silent)
		QMessageBox::critical(this, tr("Error checking for updates"), tr(errorMessage.toUtf8().data()));
}
