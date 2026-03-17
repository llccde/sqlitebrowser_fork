#include"DiffHelper.h"
#include"sqlitetablemodel.h"
#include "sqlite.h"
#include <QDebug>
#include <QRegularExpression>
#include <sstream>
#include <iomanip>
#include"qobject.h"
#include"sqlitedb.h"
#include"RowLoader.h"

const QString DiffWorker::workOK("done");

namespace {
    int notFound = -1;
    int binarySearch(const QVector<int>& vec, int value) {
        int left = 0;
        int right = vec.size() - 1;
        while (left <= right) {
            int mid = left + (right - left) / 2; // 防止溢出
            if (vec[mid] == value) {
                return mid;          // 找到，返回索引
            }
            else if (vec[mid] < value) {
                left = mid + 1;       // 在右半部分继续查找
            }
            else {
                right = mid - 1;      // 在左半部分继续查找
            }
        }
        return -1; // 未找到
    };
    const QString ErrorCode = "<ERROR!>";
    QString columnToString(sqlite3_stmt* stmt, int col, int type = -1)
    {
        if (type == -1)type = sqlite3_column_type(stmt, col);

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
            return ErrorCode;
        }
    }
    int mapStringToSqliteType(const QString& typeStr) {
        QString upper = typeStr.toUpper();
        if (upper.contains("INT")) return SQLITE_INTEGER;   // INTEGER, INT, BIGINT 等
        if (upper.contains("CHAR") || upper.contains("TEXT") || upper.contains("CLOB"))
            return SQLITE_TEXT;
        if (upper.contains("BLOB")) return SQLITE_BLOB;
        if (upper.contains("REAL") || upper.contains("FLOA") || upper.contains("DOUB"))
            return SQLITE_FLOAT;
        // 其他类型（如 NUMERIC）可视为 SQLITE_NUMERIC，但 SQLite 本身没有该宏，可按需处理
        return SQLITE_NULL;   // 默认或未知类型
    }
    class StringCacheWritter:public CacheWritter {
        // 通过 CacheWritter 继承
    public:
        Cache_T<QString>& cache;
        Cache_T_value<QString> current;
        std::mutex& mutex;

        std::mutex& getLock() override
        {
            return mutex;
        }
        void startRow(int num) override
        {
            current.clear();
            current.resize(num);
        }
        void writeColumn(sqlite3_stmt* stmt, int column) override
        {
            auto str = columnToString(stmt, column);
            assert(column < current.size());
            current[column] = str;

        }
        void writeRow(int row) override
        {
            cache.set(row, std::move(current));
            current = Cache_T_value<QString>();
        }

        StringCacheWritter(Cache_T<QString>& cache,std::mutex& mutex)
            : cache(cache), current(current), mutex(mutex)
        {
        }
    };
}


DiffWorker::DiffWorker(DBBrowserDB* _db) :_db(_db)
{
    db_getter = [this]() {
        return this->_db->get("do diff",true);
        };
    this->rowLoader.reset(new RowLoader(db_getter, 
        [](QString s) {qWarning()<<s;}, 
        headers,
        std::unique_ptr<StringCacheWritter>( new StringCacheWritter(my_cache,CacheMutex))
    ));
    rowLoader->start();
    

    QObject::connect(this, &DiffWorker::startRecord,
        this, &DiffWorker::recordOld,
        Qt::ConnectionType::QueuedConnection);
    QObject::connect(this, &DiffWorker::startDiff,
        this, &DiffWorker::diff,
        Qt::ConnectionType::QueuedConnection);
    connect(rowLoader.get(), &RowLoader::fetched, this, [this](int t,size_t b,size_t e){
        emit this->newDataLoadDone(b,e);
    });

    oldTable.reset(new OldTable());
    this->moveToThread(&thread);
    this->launch();
}


void DiffWorker::setTable(const QString& tableName) {
#ifdef NDEBUG
#else
    auto tables = _db->schemata["main"].tables;
    auto table = tables.find(tableName.toStdString());
    assert(table != tables.end());
#endif
    
    this->tableName = tableName;
    selectQuery = QString("SELECT * FROM %1").arg(tableName);
    {
        std::lock_guard l(CacheMutex);
    }
    this->my_cache.clear();
    //加载部分数据
    rowLoader->setQuery(selectQuery);


}

void DiffWorker::loadHead(bool callInInitLine)
{
    head.clear();

    auto PDB = db_getter();
    if (!PDB) {
        return;
    }

    QString pragmaQuery = QString("PRAGMA table_info(%1)").arg(tableName);
    QByteArray utf8Pragma = pragmaQuery.toUtf8();
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(PDB.get(), utf8Pragma, utf8Pragma.size(), &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            // PRAGMA table_info 返回的列：
            // 0: cid, 1: name, 2: type, 3: notnull, 4: dflt_value, 5: pk
            const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* colType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            QString name = colName ? QString::fromUtf8(colName) : QString();
            QString typeStr = colType ? QString::fromUtf8(colType) : QString();

            // 将类型字符串映射为 SQLite 类型常量（可选）
            int type = mapStringToSqliteType(typeStr);

            head.append(column(name, static_cast<columnType>(type)));
        }
        sqlite3_finalize(stmt);
    }
    else {

    }
    if(!callInInitLine) emit currentTableHeadChanged();

}

void DiffWorker::refreshCurrentTable()
{
    my_cache.clear();
    loadHead();
    loadData(currentRowIndex);
}

int DiffWorker::getRowCount() {
    // 如果表名为空，直接返回0
    if (tableName.isEmpty()) {
        return 0;
    }
    // 获取数据库连接
    auto PDB = db_getter();
    if (!PDB) {
        return 0;
    }

    // 构建 COUNT 查询
    QString countQuery = QString("SELECT COUNT(*) FROM %1").arg(tableName);
    QByteArray utf8Query = countQuery.toUtf8();
    sqlite3_stmt* stmt = nullptr;
    int rowCount = 0;

    // 准备并执行语句
    if (sqlite3_prepare_v2(PDB.get(), utf8Query, utf8Query.size(), &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            rowCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    else {
        // 可选：记录错误日志
        // qWarning() << "Failed to prepare count query for table:" << tableName;
    }

    return rowCount;
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
                rowdata.append(columnToString(stmt,static_cast<int>(i),-1));
            }
            tableData->table.append(std::move(rowdata));
        }
        sqlite3_finalize(stmt);
    }
    
    {
        std::lock_guard l(swapOldTablePtr);
        oldTable = std::move(tableData);
        for (size_t i = 0; i < oldTable->table.size(); i++){
            oldTableIndex.insert(OldTableKey(i, this), i);
        }
    }
    emit recordOldDone();

}

DiffWorker::~DiffWorker()
{
    stop();
    thread.quit();
    thread.wait();
    rowLoader->stop();
    rowLoader->wait();
    rowLoader->disconnect();
}


void DiffWorker::diff(WorkID ID)
{
}

void DiffWorker::loadData(long long row)
{

    rowLoader->triggerFetch(rowLoaderWorkID,
        (row - chunkSize >= 0) ? (row - chunkSize) : 0,
        row + chunkSize
    );
    rowLoaderWorkID++;
}

//bool DiffWorker::isRowInCurrent_New(int row)
//{
//    std::lock_guard l(swapDiffResultPtr);
//    if (binarySearch(diffResult->addedRowInCurrent,row) != notFound){
//        return true;
//    }
//    else{
//        return false;
//    }
//}
//
//bool DiffWorker::isRowinOld_Removed(int row)
//{
//    std::lock_guard l(swapDiffResultPtr);
//    return binarySearch(diffResult->removedRowInOld, row) != notFound;
//}
//
//bool DiffWorker::isRowInCurrent_Changed(int row)
//{
//    std::lock_guard l(swapDiffResultPtr);
//    return binarySearch(diffResult->changedRowIncurrent, row) != notFound;
//}
//
//bool DiffWorker::isRowInOld_Changed(int row)
//{
//    std::lock_guard l(swapDiffResultPtr);
//    return false;
//}
//
//QVector<bool> DiffWorker::isColumnInCurrent_Changed(int rowInCurrent)
//{
//    std::lock_guard l(swapDiffResultPtr);
//    return QVector<bool>();
//}
//
//QVector<bool> DiffWorker::isColumnOld_Changed(int rowInOld)
//{
//    std::lock_guard l(swapDiffResultPtr);
//    return QVector<bool>();
//}
//

