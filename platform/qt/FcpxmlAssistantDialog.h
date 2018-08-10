/*!
 * \file FcpxmlAssistantDialog.h
 * \author masc4ii
 * \copyright 2018
 * \brief Import MLV files to session, which were used in FCPXML project
 */

#ifndef FCPXMLASSISTANTDIALOG_H
#define FCPXMLASSISTANTDIALOG_H

#include <QDialog>

namespace Ui {
class FcpxmlAssistantDialog;
}

class FcpxmlAssistantDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FcpxmlAssistantDialog(QWidget *parent = 0);
    ~FcpxmlAssistantDialog();
    QStringList getFileNames( void );

private slots:
    void on_pushButtonFcpxml_clicked();
    void on_pushButtonMlv_clicked();

private:
    Ui::FcpxmlAssistantDialog *ui;
    QStringList m_fileList;
    void xmlParser( QString fileName );
    void searchMlvs();
};

#endif // FCPXMLASSISTANTDIALOG_H
