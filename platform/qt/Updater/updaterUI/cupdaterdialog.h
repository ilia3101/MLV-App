#ifndef CUPDATERDIALOG_H
#define CUPDATERDIALOG_H

#include "../Updater.h"
#include <QDialog>
#include <QTimer>

namespace Ui {
	class CUpdaterDialog;
}

class CUpdaterDialog : public QDialog
{
public:
	explicit CUpdaterDialog(QWidget *parent, const QString& githubRepoAddress, const QString& versionString, bool silentCheck = false);
	~CUpdaterDialog();

private slots:
    void checkUpdate( void );

private:
	Ui::CUpdaterDialog *ui;
    void applyUpdate();

    const bool _silent;
	QString _latestUpdateUrl;
    Updater _updater;
};

#endif // CUPDATERDIALOG_H
