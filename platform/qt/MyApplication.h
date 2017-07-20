#ifndef MYAPPLICATION_H
#define MYAPPLICATION_H

#include <QApplication>
#include <QFileOpenEvent>
#include <QtDebug>
#include <QWindow>

//Only for FileOpenEvent...
class MyApplication : public QApplication
{
public:
    MyApplication(int &argc, char **argv)
        : QApplication(argc, argv)
    {
    }

    bool event(QEvent *event)
    {
        // Intercept FileOpen events
        if (event->type() == QEvent::FileOpen) {
            //Send Event to QMainWindow
            QWindowList WinList( topLevelWindows() );
            QWindowList::iterator pIt = WinList.begin();
            if( pIt == WinList.end() ) return false;
            QWindow * pMainWin( *pIt );
            return sendEvent( pMainWin, event );
        }

        return QApplication::event(event);
    }
};

#endif // MYAPPLICATION_H
