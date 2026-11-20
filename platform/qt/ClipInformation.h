/*!
 * \file ClipInformation.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents one clip in the session: metadata + receipt
 */

#ifndef CLIPINFORMATION_H
#define CLIPINFORMATION_H

#include <QObject>
#include <QColor>
#include <QIcon>
#include <array>
#include "ReceiptSettings.h"

class ClipInformation
{
public:
    enum Element {
        Name,
        Path,
        Camera,
        Lens,
        Resolution,
        Duration,
        Frames,
        FrameRate,
        FocalLength,
        FocusDistance,
        Shutter,
        Aperture,
        Iso,
        DualIso,
        BitDepth,
        DateTime,
        Audio,
        Size,
        DataRate,
        Count
    };

    ClipInformation( QString name, QString path );
    ~ClipInformation();

    void setPathName( QString name, QString path );

    QVariant getElement( int element ) const;

    void setElement( int element, QVariant value );

    QString getName() const {
        return m_elements[Name];
    }

    QString getPath() const {
        return m_elements[Path];
    }

    QColor getBackgroundColor() const {
        return m_backgroundColor;
    }

    void setBackgroundColor( QColor color ) {
        m_backgroundColor = color;
    }

    static int getPreviewPaddingTop() {
        return m_previewPaddingTop;
    }

    static int getPreviewMinWidth() {
        return m_previewMinWidth;
    }

    static double getPreviewRatio() {
        return m_previewRatio;
    }

    QIcon getPreview() const {
        return m_preview;
    }

    void setPreview( const QPixmap &src );

    ReceiptSettings* getReceipt() {
        return &m_receipt;
    }

    const ReceiptSettings* getReceipt() const {
        return &m_receipt;
    }

private:
    quint64 clipSize() const;

    const QString m_placeholderEmpty = "–";

    std::array<QString, Count> m_elements{};

    QColor m_backgroundColor = Qt::transparent;

    static const int m_previewPaddingTop = 4;
    static const int m_previewMinWidth = 100;
    static constexpr double m_previewRatio = 16.0 / 9.0;
    const qreal m_radius = 4;

    QIcon m_preview;

    ReceiptSettings m_receipt;
};

#endif // CLIPINFORMATION_H
