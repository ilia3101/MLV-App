#include "UserManualDialog.h"
#include "ui_UserManualDialog.h"
#include <QFile>
#include <QTextStream>

UserManualDialog::UserManualDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UserManualDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Tool | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint );

    QFile file(":/help/help/help.htm");
    file.open(QFile::ReadOnly | QFile::Text);
    QTextStream stream(&file);

    ui->textBrowser->setHtml(R"(
        <html>
            <head>
                <meta name="qrichtext" content="1" />
                <meta charset="utf-8" />
                <style type="text/css">
                    body {
                        margin: 0 20px;
                    }
                    h1, h2, h3, p, ul, ol {
                        margin: 20px 0;
                    }
                </style>
            </head>
            <body>)" + stream.readAll() + R"(</body>
        </html>
    )");
}

UserManualDialog::~UserManualDialog()
{
    delete ui;
}
