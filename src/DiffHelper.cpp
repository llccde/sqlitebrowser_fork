#include"DiffHelper.h"
#include"sqlitetablemodel.h"
#include "sqlite.h"
#include <QDebug>
#include <QRegularExpression>
#include <sstream>
#include <iomanip>
#include"qobject.h"
namespace DiffHelp {
    QString rtrimChar(const QString& s, QChar c)
    {
        QString r = s.trimmed();
        while (r.endsWith(c))
            r.chop(1);
        return r;
    }
    QString columnToString(sqlite3_stmt* stmt, int col, int type = -1) {
        if(type == -1)type = sqlite3_column_type(stmt, col);

        switch (type) {
        case SQLITE_INTEGER:
            return QString::number(sqlite3_column_int64(stmt, col));

        case SQLITE_FLOAT:
            return QString::number(sqlite3_column_double(stmt, col));

        case SQLITE_TEXT: {
            const char* text = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, col));
            return text ? text : "";
        }

        case SQLITE_BLOB: {
            int size = sqlite3_column_bytes(stmt, col);
            const unsigned char* blob = static_cast<const unsigned char*>(
                sqlite3_column_blob(stmt, col));
            std::ostringstream hex;
            hex << std::hex << std::setfill('0');
            for (int i = 0; i < size; ++i) {
                hex << std::setw(2) << static_cast<int>(blob[i]);
            }
            return QString::fromStdString(hex.str());
        }

        case SQLITE_NULL: {
            return"";
        }
        default:
            assert(false);
            return "<ERROR!>";
        }
    }
}
DiffWorker::DiffWorker(DBBrowserDB* _db) :_db(_db)
{
    db_getter = [this]() {
        return this->_db->get("do diff");
        };
    QObject::connect(this, &DiffWorker::startRecord,
        this, &DiffWorker::recordOld,
        Qt::ConnectionType::QueuedConnection);
    QObject::connect(this, &DiffWorker::startDiff,
        this, &DiffWorker::diff,
        Qt::ConnectionType::QueuedConnection);
    this->moveToThread(&thread);
    this->launch();
}

void DiffWorker::setTable(const QString& tableName){
#ifdef NDEBUG
#else
    auto tables = _db->schemata["main"].tables;
    auto table = tables.find(tableName.toStdString());
    assert(table != tables.end());
#endif
    this->tableName = tableName;
    selectQuery = QString("SELECT * FROM %1").arg(tableName);

}
void DiffWorker::recordOld(WorkID ID){


    QString query = selectQuery;
    auto PDB = db_getter();
    assert(PDB != nullptr);
    std::unique_ptr<DiffWorker::OldTable> tableData(new OldTable());

    QByteArray utf8Query = query.toUtf8();
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(PDB.get(), utf8Query, utf8Query.size(), &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            if (workID != ID) {
                return;
            }
            size_t num_columns = static_cast<size_t>(sqlite3_data_count(stmt));
            
            QVector<QString> rowdata;
            for (size_t i = 0;i < num_columns;++i)
            {
                rowdata.append(DiffHelp::columnToString(stmt,static_cast<int>(i)));
            }
            tableData->table.append(std::move(rowdata));
        }
        sqlite3_finalize(stmt);
    }
    
    {
        std::lock_guard l(swapCache);
        oldTable = std::move(tableData);
    }
    

}

void DiffWorker::diff(WorkID ID)
{
}
