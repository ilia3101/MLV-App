/*!
 * \file PixelMapListDialog.cpp
 * \author masc4ii
 * \copyright 2019
 * \brief Show all installed focus pixel maps
 */

#include "PixelMapListDialog.h"
#include "ui_PixelMapListDialog.h"
#include <QDir>
#include <QDebug>
#include <QTreeWidgetItem>

#ifdef __cplusplus
extern "C" {
#endif

#include <../../src/mlv/camid/camera_id.h>
extern int camidCheckIfCameraKnown(uint32_t cameraModel);
extern const char* camidGetCameraName(uint32_t cameraModel, int camname_type);

#ifdef __cplusplus
}
#endif

//Constructor
PixelMapListDialog::PixelMapListDialog(QWidget *parent, MapType mapType) :
    QDialog(parent),
    ui(new Ui::PixelMapListDialog)
{
    ui->setupUi(this);
    setWindowFlags( Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint );

    m_mapType = mapType;
    ui->labelMissing->setVisible( false );

    QDir directory( QCoreApplication::applicationDirPath() );
    QStringList fpms;
    if( m_mapType == FPM )
    {
        fpms = directory.entryList( QStringList() << "*.fpm" << "*.FPM", QDir::Files );
        setWindowTitle( "Installed Focus Pixel Maps" );
    }
    else
    {
        fpms = directory.entryList( QStringList() << "*.bpm" << "*.BPM", QDir::Files );
        setWindowTitle( "Installed Bad Pixel Maps" );
    }

    //For all maps
    while( fpms.count() > 0 )
    {
        QString map = fpms.first();
        bool ok;
        uint32_t camId = map.left( 8 ).toUInt( &ok, 16 );
        //Unknown cameras are not shown here
        if( !ok || 0 == camidCheckIfCameraKnown( camId ) )
        {
            fpms.removeAll( map );
            qDebug() << ok << camId << map.left( 8 );
            continue;
        }
        //Camera name
        QTreeWidgetItem *cams = new QTreeWidgetItem(ui->treeWidget);
        QString camName = QString( "%1" ).arg( camidGetCameraName( camId, 0 ) );
        if( camidGetCameraName( camId, 1 ) != NULL ) camName.append( QString( " / %1" ).arg( camidGetCameraName( camId, 1 ) ) );
        if( camidGetCameraName( camId, 2 ) != NULL ) camName.append( QString( " / %1" ).arg( camidGetCameraName( camId, 2 ) ) );
        cams->setText( 0, camName );
        //Maps
        foreach( QString map2, fpms )
        {
            if( map2.contains( map.left( 8 ) ) )
            {
                QTreeWidgetItem *fpmItem = new QTreeWidgetItem( cams );
                fpmItem->setText( 0, map2 );
                fpms.removeAll( map2 );
            }
        }
    }
}

PixelMapListDialog::~PixelMapListDialog()
{
    delete ui;
}

//Mark used fpm in tree structure
void PixelMapListDialog::showCurrentMap( mlvObject_t *pMlvObject )
{
    QString fileType;
    if( m_mapType == FPM ) fileType = "fpm";
    else fileType = "bpm";

    QString name = QString( "%1_%2x%3.%4" ).arg( pMlvObject->IDNT.cameraModel, 0, 16 )
                                           .arg( pMlvObject->RAWI.raw_info.width )
                                           .arg( pMlvObject->RAWI.raw_info.height )
                                           .arg( fileType );

    bool foundMap = false;
    for( int i = 0; i < ui->treeWidget->topLevelItemCount(); i++ )
    {
        for( int j = 0; j < ui->treeWidget->topLevelItem( i )->childCount(); j++ )
        {
            if( ui->treeWidget->topLevelItem( i )->child( j )->text( 0 ) == name )
            {
                ui->treeWidget->expandItem( ui->treeWidget->topLevelItem( i ) );
                foundMap = true;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                ui->treeWidget->topLevelItem( i )->child( j )->setTextColor( 0, Qt::green );
#else
                ui->treeWidget->topLevelItem( i )->child( j )->setForeground( 0, Qt::green );
#endif
            }
        }
    }
    ui->labelMissing->setVisible( !foundMap );
    if( !foundMap ) ui->labelMissing->setText( QString( "Map missing: %1" ).arg( name ) );
}
