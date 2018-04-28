/*!
 * \file Scripting.cpp
 * \author masc4ii
 * \copyright 2018
 * \brief Scripting class
 */

#include "Scripting.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QProcess>
#include <QMessageBox>

//Constructur
Scripting::Scripting(QObject *parent) :
    QObject( parent )
{
    scanScripts();
    m_postExportScriptIndex = 0;
    m_isTiff = false;
}

//Scan all installed scripts in application folder
void Scripting::scanScripts()
{
    QDir *pDir = new QDir( QCoreApplication::applicationDirPath() );
    QStringList filters;
    filters << "*.command";
    m_scriptNames = pDir->entryList( filters, QDir::Files, QDir::Name );
    m_scriptNames.prepend( "None" );
    m_postExportScriptIndex = 0;
}

//Read out scanned script file list
QStringList Scripting::getScriptNames()
{
    return m_scriptNames;
}

//Set post export script by file name
void Scripting::setPostExportScript(QString name)
{
    int index = m_scriptNames.indexOf( name );
    if( index > 0 ) m_postExportScriptIndex = index;
    else m_postExportScriptIndex = 0;
}

//Set post export script by index
void Scripting::setPostExportScript(uint16_t index)
{
    m_postExportScriptIndex = index;
}

//Set export directory
void Scripting::setExportDir(QString dir)
{
    m_exportDir = dir;
}

//Define that next script looks for tiff and fps file
void Scripting::setNextScriptInputTiff(float fps, QString folderName)
{
    //qDebug() << QString( "%1/fps" ).arg( folderName ) << m_postExportScriptIndex;

    if( m_postExportScriptIndex )
    {
        m_isTiff = true;
        m_fps = fps;

        //Solving the . and , problem at fps in the command
        QLocale locale = QLocale(QLocale::English, QLocale::UnitedKingdom);
        locale.setNumberOptions(QLocale::OmitGroupSeparator);

        //we also need to know HDR fps from the actual MLV files since tif doesn´t reveal frames per second.
        QString fps = locale.toString( m_fps );
        QFile file( QString( "%1/fps" ).arg( folderName ) );
        file.open(QIODevice::WriteOnly);
        file.write(fps.toUtf8());
        file.close();
    }
}

//Get name of chosen export script
QString Scripting::postExportScriptName()
{
    return m_scriptNames.at( m_postExportScriptIndex );
}

//Get index of chosen export script
uint16_t Scripting::postExportScriptIndex()
{
    return m_postExportScriptIndex;
}

//Start chosen post export script
void Scripting::executePostExportScript()
{
    //if none selected do nothing
    if( !m_postExportScriptIndex ) return;

    //path to MacOS folder(applications. Before appending
    QString filename2 = "/tmp/Data2.txt";
    QFile file2(filename2);
    file2.open(QIODevice::WriteOnly);
    file2.write(QCoreApplication::applicationDirPath().toUtf8());
    file2.close();

    //path to output folder. Needed for bash script workflows
    QString filename = "/tmp/Data.txt";
    QFile file1(filename);
    file1.open(QIODevice::WriteOnly);
    file1.write(m_exportDir.toUtf8());
    file1.close();

    if( m_isTiff )
    {
        //working with HDR tif files. Bash script need to know about this. Send a file tmp which bash can look for
        QString filename3 = "/tmp/tif_creation";
        QFile file3(filename3);
        file3.open(QIODevice::WriteOnly);
        file3.close();
    }

    //enabling HDR processing, no questions asked. Yet.
    QProcess process;
    process.startDetached("/bin/bash", QStringList()<< "HDR_MOV.command");

    //Cleanup temp files
    /*
    QFile( "/tmp/Data2.txt" ).remove();
    QFile( "/tmp/Data.txt" ).remove();
    if( m_isTiff )
    {
        QFile( "/tmp/tif_creation" ).remove();
        QFile( m_exportDir + "/fps" ).remove();
    }
    */

    //set back for next time
    m_isTiff = false;
}

//Install a new script to MLVApp
bool Scripting::installScript(QString fileName)
{
    bool ret = QFile::copy( fileName, QCoreApplication::applicationDirPath().append( "/" ).append( QFileInfo(fileName).fileName() ) );
    if( ret )
    {
        scanScripts();
    }
    return ret;
}
