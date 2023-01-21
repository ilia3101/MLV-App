/*!
 * \file SessionModel.cpp
 * \author masc4ii
 * \copyright 2020
 * \brief This class represents the data for the session list and table
 */

#include "SessionModel.h"
#include <QColor>
#include <QDebug>
#include <QFile>

//Constructor
SessionModel::SessionModel(QObject *parent) : QAbstractItemModel(parent), m_activeRow( -1 )
{
    m_headers = QStringList();
    m_headers << "Name" << "Path" << "Camera" << "Lens" << "Resolution" << "Duration" << "Frames" << "Frame Rate" << "Focal Length" << "Shutter" << "Aperture" << "ISO" << "DualISO" << "Bit Depth" << "Date / Time" << "Audio" << "Size" << "Data Rate";
}

//Read data for the session table / list
QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if( role == Qt::DisplayRole )
    {
        return m_dataBase.at( index.row() )->getElement( index.column() );
    }
    if( role == Qt::BackgroundRole )
    {
        return m_dataBase.at( index.row() )->getBackgroundColor();
    }
    if( role == Qt::ForegroundRole )
    {
        if( index.row() == m_activeRow ) return QColor( 255, 154, 50, 255 );
        return QVariant();
    }
    if( role == Qt::ToolTipRole )
    {
        if( index.column() == 0 ) return m_dataBase.at( index.row() )->getPath();
        else return m_dataBase.at( index.row() )->getElement( index.column() );
    }
    if( role == Qt::DecorationRole && index.column() == 0 )
    {
        return m_dataBase.at( index.row() )->getPreview();
    }
    if( role == ROLE_REALINDEX )
    {
        return index.row();
    }

    return QVariant();
}

//The whole table is read only
Qt::ItemFlags SessionModel::flags(const QModelIndex &index) const
{
    Q_UNUSED( index );
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

//Get the header data for the session table
QVariant SessionModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal)
    {
        if( role == Qt::DisplayRole)
        {
            return m_headers.at(section);
        }
    }
    else
    {
        if( role == Qt::DisplayRole)
        {
            return QVariant();
        }
    }

    return QVariant();
}

//Get the index
QModelIndex SessionModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED( parent );
    return createIndex(row, column);
}

//Get parent
QModelIndex SessionModel::parent(const QModelIndex &index) const
{
    Q_UNUSED( index );
    return QModelIndex();
}

//Read row (clip) count
int SessionModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED( parent );
    return m_dataBase.count();
}

//Read column (number of shown metadata) count
int SessionModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED( parent );
    return m_headers.count();
}

//Set data to the table (Icon, Font color, Background color)
bool SessionModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if( role == Qt::DisplayRole )
    {
        m_dataBase.at( index.row() )->setElement( index.column(), value );
        return true;
    }
    if( role == Qt::BackgroundRole )
    {
        m_dataBase.at( index.row() )->setBackgroundColor( index.column() );
        return true;
    }
    if( role == Qt::DecorationRole )
    {
        m_dataBase.at( index.row() )->setPreview( value.value<QIcon>() );
        return true;
    }
    return false;
}

//Show the active clip (clip in editor is marked with orange font)
void SessionModel::setActiveRow(int row)
{
    m_activeRow = row;
}

//Read the number of the active clip
int SessionModel::activeRow()
{
    return m_activeRow;
}

//Get the data for the active clip
ClipInformation *SessionModel::activeClip()
{
    return m_dataBase.at( activeRow() );
}

//Get the data for any clip
ClipInformation *SessionModel::clip(int row)
{
    return m_dataBase.at( row );
}

//Append a new clip to the session
void SessionModel::append(ClipInformation *clip)
{
    beginInsertRows( QModelIndex(), m_dataBase.count(), m_dataBase.count() );
    m_dataBase.push_back( clip );
    endInsertRows();
}

//Delete a clip from the session
void SessionModel::removeRow(int row, const QModelIndex &parent)
{
    beginRemoveRows( parent, row, row );
    m_dataBase.removeAt( row );
    endRemoveRows();
}

//Delete the whole session
void SessionModel::clear()
{
    if( m_dataBase.count() <= 0 ) return;
    beginRemoveRows( QModelIndex(), 0, m_dataBase.count() - 1 );
    m_dataBase.clear();
    endRemoveRows();
}

//Get the receipt for any clip
ReceiptSettings *SessionModel::receipt(int row)
{
    return m_dataBase.at( row )->getReceipt();
}

//Export a csv table of session clips metadata
void SessionModel::writeMetadataToCsv(QString fileName)
{
    //Write file
    QFile data( fileName );
    if( data.open( QFile::WriteOnly | QFile::Truncate ) )
    {
        QTextStream out(&data);

        //Header
        for( int j = 0; j < columnCount( QModelIndex() ); j++ )
        {
            if( j ) out << "\t";
            out << headerData( j, Qt::Horizontal, Qt::DisplayRole ).toString();
        }
        out << '\n';
        //Data
        for( int i = 0; i < rowCount( QModelIndex() ); i++ )
        {
            for( int j = 0; j < columnCount( QModelIndex() ); j++ )
            {
                if( j ) out << "\t";
                out << clip( i )->getElement( j ).toString();
            }
            out << '\n';
        }
        data.close();
    }
}
