/*!
 * \file MoveToTrash.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Move a file to recycle bin / trash
 */

#include "MoveToTrash.h"
#include <QtGui>
#include <stdio.h>
#include <stdlib.h>
#include <QString>
#include <QMessageBox>
#ifdef Q_OS_WIN
#include "windows.h"
#endif

//Move file to recycle bin / trash
int MoveToTrash( QString fileName )
{
    //########################## OSX ##########################
#ifdef Q_OS_MAC
    QString command = QString( "osascript -e 'set theFile to POSIX file \"%1\"' "
                      "-e 'tell application \"Finder\"' "
                      "-e 'delete theFile' "
                      "-e 'end tell' "
                      ">/dev/null" ).arg( fileName );

    int status = system( command.toLatin1().data() );
    //qDebug() << "Move file to trash status:" << status;
    return status;
#else
    return 1; //to be removed...
#endif
/*
    //########################## WINDOWS ##########################
#ifdef !Q_OS_WIN
    QFileInfo fileinfo( fileName );
    if( !fileinfo.exists() )
        return 1;
        //throw OdtCore::Exception( "File doesnt exists, cant move to trash" );
    WCHAR from[ MAX_PATH ];
    memset( from, 0, sizeof( from ));
    int l = fileinfo.absoluteFilePath().toWCharArray( from );
    Q_ASSERT( 0 <= l && l < MAX_PATH );
    from[ l ] = '\0';
    SHFILEOPSTRUCT fileop;
    memset( &fileop, 0, sizeof( fileop ) );
    fileop.wFunc = FO_DELETE;
    fileop.pFrom = from;
    fileop.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    int rv = SHFileOperation( &fileop );
    if( 0 != rv ){
        qDebug() << rv << QString::number( rv ).toInt( 0, 8 );
        //throw OdtCore::Exception( "move to trash failed" );
    }
    return rv;
#endif

    //########################## LINUX ##########################
#ifdef Q_OS_LINUX
#ifdef QT_GUI_LIB
    static bool TrashInitialized = false;
    static QString TrashPath;
    static QString TrashPathInfo;
    static QString TrashPathFiles;
    if( !TrashInitialized ){
        QStringList paths;
        const char* xdg_data_home = getenv( "XDG_DATA_HOME" );
        if( xdg_data_home ){
            qDebug() << "XDG_DATA_HOME not yet tested";
            QString xdgTrash( xdg_data_home );
            paths.append( xdgTrash + "/Trash" );
        }
        QString home = QStandardPaths::writableLocation( QStandardPaths::HomeLocation );
        paths.append( home + "/.local/share/Trash" );
        paths.append( home + "/.trash" );
        foreach( QString path, paths ){
            if( TrashPath.isEmpty() ){
                QDir dir( path );
                if( dir.exists() ){
                    TrashPath = path;
                }
            }
        }
        if( TrashPath.isEmpty() )
            return 1;
            //throw Exception( "Cant detect trash folder" );
        TrashPathInfo = TrashPath + "/info";
        TrashPathFiles = TrashPath + "/files";
        if( !QDir( TrashPathInfo ).exists() || !QDir( TrashPathFiles ).exists() )
            return 2;
            //throw Exception( "Trash doesnt looks like FreeDesktop.org Trash specification" );
        TrashInitialized = true;
    }
    QFileInfo original( fileName );
    if( !original.exists() )
        return 3;
        //throw Exception( "File doesnt exists, cant move to trash" );
    QString info;
    info += "[Trash Info]\nPath=";
    info += original.absoluteFilePath();
    info += "\nDeletionDate=";
    info += QDateTime::currentDateTime().toString("yyyy-MM-ddThh:mm:ss.zzzZ");
    info += "\n";
    QString trashname = original.fileName();
    QString infopath = TrashPathInfo + "/" + trashname + ".trashinfo";
    QString filepath = TrashPathFiles + "/" + trashname;
    int nr = 1;
    while( QFileInfo( infopath ).exists() || QFileInfo( filepath ).exists() ){
        nr++;
        trashname = original.baseName() + "." + QString::number( nr );
        if( !original.completeSuffix().isEmpty() ){
            trashname += QString( "." ) + original.completeSuffix();
        }
        infopath = TrashPathInfo + "/" + trashname + ".trashinfo";
        filepath = TrashPathFiles + "/" + trashname;
    }
    QDir dir;
    if( !dir.rename( original.absoluteFilePath(), filepath ) ){
        return 4;
        //throw Exception( "move to trash failed" );
    }
    File infofile;
    infofile.createUtf8( infopath, info );
    return 0;
#else
    Q_UNUSED( fileName );
    //throw Exception( "Trash in server-mode not supported" );
    return 5;
#endif
#endif
*/
}
