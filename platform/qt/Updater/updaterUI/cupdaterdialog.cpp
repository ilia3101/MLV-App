#include "cupdaterdialog.h"

#include "ui_cupdaterdialog.h"

#include <QDebug>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>
#include <QScrollBar>
#include "maddy/parser.h"
#include <memory>
#include <string>

CUpdaterDialog::CUpdaterDialog(QWidget *parent, const QString& githubRepoAddress, const QString& versionString, bool silentCheck) :
	QDialog(parent),
	ui(new Ui::CUpdaterDialog),
	_silent(silentCheck),
    _updater(this, QUrl(githubRepoAddress), versionString)
{
	ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint );

	if (_silent)
		hide();

	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CUpdaterDialog::applyUpdate);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText("Download");

	ui->stackedWidget->setCurrentIndex(0);
	ui->progressBar->setMaximum(0);
	ui->progressBar->setValue(0);
	ui->lblPercentage->setVisible(false);

    QTimer::singleShot( 1, this, &CUpdaterDialog::checkUpdate );
}

CUpdaterDialog::~CUpdaterDialog()
{
    delete ui;
}

void CUpdaterDialog::checkUpdate( void )
{
    if( !_updater.isUpdateAvailable() )
    {
        accept();
        if (!_silent)
            QMessageBox::information(this, tr("No update available"), tr("You already have the latest version of MLV App."));
        return;
    }

    ui->stackedWidget->setCurrentIndex(1);
    QString text;
    for (const auto& changelogItem: _updater.getUpdateChangelog())
    {
        text.append("<b>" % changelogItem.versionString % "</b>" % "\r\n\r\n" % changelogItem.versionChanges % "<p></p>" % "\r\n\r\n" );
    }
    //ui->changeLogViewer->setMarkdown( text ); //Just for Qt5.14+
    // ////
    //Use Maddy parser Markdown->Html
    std::shared_ptr<maddy::ParserConfig> config = std::make_shared<maddy::ParserConfig>();
    config->isEmphasizedParserEnabled = true; // default
    config->isHTMLWrappedInParagraph = true; // default
    std::shared_ptr<maddy::Parser> parser = std::make_shared<maddy::Parser>(config);
    std::stringstream markdownInput( text.toStdString() );
    std::string htmlOutput = parser->Parse( markdownInput );
    // ////
    ui->changeLogViewer->setText( QString::fromStdString( htmlOutput ) );

    _latestUpdateUrl = _updater.getUpdateChangelog().front().versionUpdateUrl;
    QScrollBar *scrollbar = ui->changeLogViewer->verticalScrollBar();
    scrollbar->setSliderPosition(0);
    show();
}

void CUpdaterDialog::applyUpdate()
{
    QDesktopServices::openUrl(QUrl(_latestUpdateUrl));
}
