/*!
 * \file ClipInformation.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents one clip in the session: metadata + receipt
 */

#include "ClipInformation.h"
#include <QVariant>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

//Constructor
ClipInformation::ClipInformation(QString name, QString path)
{
    m_elements[Name] = name;
    m_elements[Path] = path;

    QPixmap pixmap = QPixmap( m_previewMinWidth * 2, m_previewMinWidth * 2 / m_previewRatio );
    pixmap.fill( Qt::black );
    setPreview( pixmap );

    m_receipt.setFileName( path );
}

void ClipInformation::setPreview( const QPixmap &src )
{
    int width = m_previewMinWidth * 2 * src.devicePixelRatio();
    width = src.width() < width ? width : src.width();
    int height = width / m_previewRatio;

    qreal factor = static_cast<qreal>( width ) / m_previewMinWidth;

    // Create thumbnail canvas
    qreal paddingTop = m_previewPaddingTop * factor;
    QPixmap thumbnail( width, height + paddingTop );

    thumbnail.fill( Qt::transparent );

    // Scale source to fit (or fill with Qt::KeepAspectRatioByExpanding)
    QPixmap scaled = src.scaled( width, width / m_previewRatio + 0.5, Qt::KeepAspectRatio, Qt::SmoothTransformation );
    scaled.setDevicePixelRatio( 1 );

    // Optional rounded corners
    qreal radius = m_radius * factor;

    QPainter painter( &thumbnail );
    painter.setRenderHint( QPainter::Antialiasing );

    QPainterPath path;
    path.addRoundedRect( 0, paddingTop, width, height, radius, radius );
    painter.setClipPath( path );

    // Center the pixmap
    qreal x = ( width - scaled.width() ) / 2.0;
    qreal y = ( height - scaled.height() ) / 2.0;

    // Only fill black if needed to prevent ugly corners
    if( x > 1 || y > 1 ) painter.fillPath( path, Qt::black );

    painter.drawPixmap( x, y + paddingTop, scaled );

    // Border
    // QPen pen( Qt::black, 2 );
    // painter.setPen( pen );
    // painter.setBrush( Qt::NoBrush );
    // painter.drawPath( path );

    m_preview = QIcon( thumbnail );
}

//Set filename and path - use when file will be renamed
void ClipInformation::setPathName( QString name, QString path )
{
    m_elements[Name] = name;
    m_elements[Path] = path;
}

//Read saved metadata
QVariant ClipInformation::getElement(int element) const
{
    switch( element )
    {
    case Size:
        return QString( "%1 MB" ).arg( clipSize() / 1024 / 1024 );

    case DataRate:
    {
        const double frames = m_elements[Frames].toDouble();

        QString frameRate = m_elements[FrameRate];
        frameRate.chop(4);

        const double fps = frameRate.toDouble();

        if( 0.0 == frames || 0.0 == fps ) return m_placeholderEmpty;

        return QString( "%1 MB/s" ).arg( clipSize() / ( frames / fps ) / 1024 / 1024, 0, 'f', 2 );
    }
    }

    if( element < 0 || element >= m_elements.size() ) return {};

    return m_elements[element].isEmpty() ? m_placeholderEmpty : m_elements[element];
}

//Save metadata to the class
void ClipInformation::setElement( int element, QVariant value )
{
    if( element < 0 || element >= m_elements.size() ) return;

    QString text = value.toString();
    text = text.isEmpty() || text == "-" ? m_placeholderEmpty : text;

    m_elements[element] = text;
}

//Calc the clip size
quint64 ClipInformation::clipSize() const
{
    quint64 calcSize = 0;
    QString mName = m_elements[Path];
    calcSize += QFileInfo( mName ).size();
    for( int nr = 0; nr < 100; nr++ )
    {
        mName.chop( 2 );
        mName.append( QString( "%1" ).arg( nr, 2, 10, QChar( '0' ) ) );
        if( QFileInfo( mName ).exists() )
        {
            calcSize += QFileInfo( mName ).size();
        }
        else
        {
            break;
        }
    }
    return calcSize;
}
