#include "MainWindow.h"
#include "MyApplication.h"

int main(int argc, char *argv[])
{
    MyApplication a(argc, argv);
    a.setAttribute(Qt::AA_UseHighDpiPixmaps);
    MainWindow w(argc, argv);
    w.show();

    return a.exec();
}
