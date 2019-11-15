/*!
 * \file TranscodeDialog.h
 * \author masc4ii
 * \copyright 2019
 * \brief Transcode any RAW to MLV and import to session
 */

#ifndef TRANSCODEDIALOG_H
#define TRANSCODEDIALOG_H

#include <QDialog>

namespace Ui {
class TranscodeDialog;
}

class TranscodeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TranscodeDialog(QWidget *parent = 0);
    ~TranscodeDialog();
    QStringList importList( void );

private slots:
    void on_pushButtonAddPics_clicked();
    void on_pushButtonTranscode_clicked();
    void on_pushButtonAddSequence_clicked();

private:
    Ui::TranscodeDialog *ui;
    QStringList m_importList;
    QString m_lastSourcePath;
    QString m_lastTargetPath;
    QStringList m_fileExt;
    QString m_filter;
    bool supportedFileType( QString fileName );
};

#endif // TRANSCODEDIALOG_H
