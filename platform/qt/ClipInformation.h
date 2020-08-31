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
#include "ReceiptSettings.h"

class ClipInformation
{
public:
    ClipInformation(QString name, QString path, QString camera, QString lens, QString resolution, QString duration, QString frames, QString frameRate, QString focalLength, QString shutter, QString aperture, QString iso, QString dualIso, QString bitDepth, QString dateTime, QString audio, QColor backgroundColor);
    ClipInformation(QString name, QString path);

    ~ClipInformation();

    QVariant getElement( int element ) const;

    void setElement( int element, QVariant value );

    QString getName() const {
        return m_name;
    }

    QString getPath() const {
        return m_path;
    }

    QColor getBackgroundColor() const {
        return m_backgroundColor;
    }

    void setBackgroundColor( QColor color ) {
        m_backgroundColor = color;
    }

    QIcon getPreview() const {
        return m_preview;
    }

    void setPreview( QIcon preview ) {
        m_preview = preview;
    }

    ReceiptSettings* getReceipt() const {
        return m_pReceipt;
    }

    void updateMetadata(QString camera, QString lens, QString resolution, QString duration, QString frames, QString frameRate, QString focalLength, QString shutter, QString aperture, QString iso, QString dualIso, QString bitDepth, QString dateTime, QString audio);

private:
    QString m_name;
    QString m_path;
    QString m_camera;
    QString m_lens;
    QString m_resolution;
    QString m_duration;
    QString m_frames;
    QString m_frameRate;
    QString m_focalLength;
    QString m_shutter;
    QString m_aperture;
    QString m_iso;
    QString m_dualIso;
    QString m_bitDepth;
    QString m_dateTime;
    QString m_audio;
    QColor m_backgroundColor;
    QIcon m_preview;
    ReceiptSettings *m_pReceipt;
};

#endif // CLIPINFORMATION_H
