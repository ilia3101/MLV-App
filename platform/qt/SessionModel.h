/*!
 * \file SessionModel.h
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents the data for the session list and table
 */

#ifndef SESSIONMODEL_H
#define SESSIONMODEL_H

#define ROLE_REALINDEX  Qt::UserRole + 1

#include <QObject>
#include <QAbstractItemModel>
#include "ClipInformation.h"

class SessionModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    SessionModel( QObject *parent );
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) override;
    void setActiveRow( int row );
    int activeRow( void );
    ClipInformation *activeClip( void );
    ClipInformation *clip( int row );
    void append(ClipInformation *clip );
    void removeRow(int row, const QModelIndex &parent);
    void clear( void );
    ReceiptSettings *receipt(int row);
    void writeMetadataToCsv( QString fileName );

private:
    QStringList m_headers;
    QList<ClipInformation*> m_dataBase;
    int m_activeRow;
};

#endif // SESSIONMODEL_H
