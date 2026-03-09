#include"DiffHelper.h"
#include"sqlitetablemodel.h"
#include "sqlite.h"
#include <QDebug>
#include <QRegularExpression>

#include"qobject.h"
#include"sqlitedb.h"
#include"RowLoader.h"

const QString DiffWorker::workOK("done");
const int DiffWorker::chunkSize(2000);

DiffWorker::DiffWorker(DBBrowserDB* _db) :_db(_db)
{
    db_getter = [this]() {
        return this->_db->get("do diff");
        };
    rowLoader->triggerFetch(1,1,1);
    QObject::connect(this, &DiffWorker::startRecord,
        this, &DiffWorker::recordOld,
        Qt::ConnectionType::QueuedConnection);
    QObject::connect(this, &DiffWorker::startDiff,
        this, &DiffWorker::diff,
        Qt::ConnectionType::QueuedConnection);
    oldTable.reset(new OldTable());
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
    if (query.isEmpty()) {
        emit recordOldDone("no table select");
        return;
    }
    assert(PDB != nullptr);
    if (PDB == nullptr) {
        emit recordOldDone("no database connection");
        return;
    }
    
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
                //auto f = &RowLoader::ex;
                rowdata.append(RowLoader::columnToStringEX(stmt,static_cast<int>(i),-1));
            }
            tableData->table.append(std::move(rowdata));
        }
        sqlite3_finalize(stmt);
    }
    
    {
        std::lock_guard l(swapCache);
        oldTable = std::move(tableData);
    }
    emit recordOldDone();

}

void DiffWorker::diff(WorkID ID)
{
}
