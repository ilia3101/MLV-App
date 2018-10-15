/*!
 * \file main.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The main... the start of the horror
 */

#include "MainWindow.h"
#include "MyApplication.h"

int main(int argc, char *argv[])
{
    MyApplication a(argc, argv);
    a.setAttribute(Qt::AA_UseHighDpiPixmaps);
#ifdef Q_OS_WIN
    a.setAttribute(Qt::AA_Use96Dpi);
#endif
    MainWindow w(argc, argv);
    w.show();

    return a.exec();
}
