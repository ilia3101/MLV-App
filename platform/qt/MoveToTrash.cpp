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

    int status = system( command.toUtf8().data() );
    //qDebug() << "Move file to trash status:" << status;
    return status;
#endif

    //########################## WINDOWS ##########################
#ifdef Q_OS_WIN
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
        qDebug() << "rv" << rv << QString::number( rv ).toInt( 0, 8 );
        //throw OdtCore::Exception( "move to trash failed" );
    }
    return rv;
#endif

    //########################## LINUX ##########################
#ifdef Q_OS_LINUX
#ifdef QT_GUI_LIB
    QFileInfo fileInfo = QFileInfo( fileName );

    QDateTime currentTime(QDateTime::currentDateTime());    // save System time

    QString trashFilePath=QDir::homePath()+"/.local/share/Trash/files/";    // trash file path contain delete files
    QString trashInfoPath=QDir::homePath()+"/.local/share/Trash/info/";     // trash info path contain delete files information

    // create file format for trash info file----- START
    QFile infoFile(trashInfoPath+fileInfo.completeBaseName()+"."+fileInfo.completeSuffix()+".trashinfo");     //filename+extension+.trashinfo //  create file information file in /.local/share/Trash/info/ folder
    infoFile.open(QIODevice::ReadWrite);
    QTextStream stream(&infoFile);         // for write data on open file
    stream<<"[Trash Info]"<<endl;
    stream<<"Path="+QString(QUrl::toPercentEncoding(fileInfo.absoluteFilePath(),"~_-./"))<<endl;     // convert path string in percentage decoding scheme string
    stream<<"DeletionDate="+currentTime.toString("yyyy-MM-dd")+"T"+currentTime.toString("hh:mm:ss")<<endl;      // get date and time format YYYY-MM-DDThh:mm:ss
    infoFile.close();

    // create info file format of trash file----- END
    QDir file;
    file.rename(fileInfo.absoluteFilePath(),trashFilePath+fileInfo.completeBaseName()+"."+fileInfo.completeSuffix());  // rename(file old path, file trash path)

    return 0;
#else
    Q_UNUSED( fileName );
    //throw Exception( "Trash in server-mode not supported" );
    return 1;
#endif
#endif
}
