#include <QDebug>
#include <QRegularExpression>
#include <sstream>
#include <iomanip>

#include "RowLoader.h"
#include "sqlite.h"


namespace {

    QString rtrimChar(const QString& s, QChar c)
    {
        QString r = s.trimmed();
        while (r.endsWith(c))
            r.chop(1);
        return r;
    }

}; // anon ns

RowLoader::RowLoader(std::function<std::shared_ptr<sqlite3>(void)> db_getter_,
    std::function<void(QString)> statement_logger_,
    std::vector<std::string>& headers_,
    std::mutex& cache_mutex_,
    Cache& cache_data_,
    std::unique_ptr<CacheWritter> cacheWritter_
)
    : db_getter(db_getter_), statement_logger(statement_logger_)
    , headers(headers_)
    , query()
    , countQuery()
    , first_chunk_loaded(false)
    , num_tasks(0)
    , pDb(nullptr)
    , stop_requested(false)
    , current_task(nullptr)
    , next_task(nullptr)
    , cacheWritter(std::move(cacheWritter_))
{
    if (cacheWritter == nullptr) {
        auto w = new DeafultCacheWritter(cache_data_,cache_mutex_);
        cacheWritter.reset(w);
    }
}

RowLoader::RowLoader(std::function<std::shared_ptr<sqlite3>(void)> db_getter_,
    std::function<void(QString)> statement_logger_,
    std::vector<std::string>& headers_,
    std::unique_ptr<CacheWritter> cacheWritter_
)
    : db_getter(db_getter_), statement_logger(statement_logger_)
    , headers(headers_)
    , query()
    , countQuery()
    , first_chunk_loaded(false)
    , num_tasks(0)
    , pDb(nullptr)
    , stop_requested(false)
    , current_task(nullptr)
    , next_task(nullptr)
    , cacheWritter(std::move(cacheWritter_))
{
}





void RowLoader::setQuery (const QString& new_query, const QString& newCountQuery)
{
    std::lock_guard<std::mutex> lk(m);
    query = new_query;
    first_chunk_loaded = false;
    if (newCountQuery.isEmpty())
        // If it is a normal query - hopefully starting with SELECT - just do a COUNT on it and return the results
        countQuery = QString("SELECT COUNT(*) FROM (%1);").arg(rtrimChar(query, ';'));
    else
        countQuery = newCountQuery;
}

void RowLoader::triggerRowCountDetermination(int token)
{
    std::unique_lock<std::mutex> lk(m);

    num_tasks++;
    nosync_ensureDbAccess();

    // do a count query to get the full row count in a fast manner
    row_counter = std::async(std::launch::async, [this, token]() {
        auto nrows = countRows();
        if(nrows >= 0)
            emit rowCountComplete(token, nrows);

        std::lock_guard<std::mutex> lk2(m);
        nosync_taskDone();
    });
}

void RowLoader::nosync_ensureDbAccess ()
{
    if(!pDb)
        pDb = db_getter();
}

std::shared_ptr<sqlite3> RowLoader::getDb () const
{
    std::lock_guard<std::mutex> lk(m);
    return pDb;
}

int RowLoader::countRows() const
{
    int retval = -1;

    // Use a different approach of determining the row count when a EXPLAIN or a PRAGMA statement is used because a COUNT fails on these queries
    if(query.startsWith("EXPLAIN", Qt::CaseInsensitive) || query.startsWith("PRAGMA", Qt::CaseInsensitive))
    {
        // So just execute the statement as it is and fetch all results counting the rows
        sqlite3_stmt* stmt;
        QByteArray utf8Query = query.toUtf8();
        if(sqlite3_prepare_v2(pDb.get(), utf8Query, utf8Query.size(), &stmt, nullptr) == SQLITE_OK)
        {
            retval = 0;
            while(sqlite3_step(stmt) == SQLITE_ROW)
                retval++;
            sqlite3_finalize(stmt);
            return retval;
        }
    } else {
        statement_logger(countQuery);
        QByteArray utf8Query = countQuery.toUtf8();

        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(pDb.get(), utf8Query, utf8Query.size(), &stmt, nullptr) == SQLITE_OK)
        {
            if(sqlite3_step(stmt) == SQLITE_ROW)
                retval = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        } else {
            qWarning() << "Count query failed: " << countQuery;
        }
    }

    return retval;
}

void RowLoader::triggerFetch (int token, size_t row_begin, size_t row_end)
{
    std::unique_lock<std::mutex> lk(m);

    if(pDb) {
        if(!row_counter.valid() || row_counter.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            // only if row count is complete, we can safely interrupt SQLite to speed up cancellation
            sqlite3_interrupt(pDb.get());
        }
    }

    if(current_task)
        current_task->cancel = true;

    nosync_ensureDbAccess();

    // (forget a possibly already existing "next task")
    next_task.reset(new Task{ *this, token, row_begin, row_end });

    lk.unlock();
    cv.notify_all();
}

void RowLoader::nosync_taskDone()
{
    if(--num_tasks == 0) {
        pDb = nullptr;
    }
}

void RowLoader::cancel ()
{
    std::unique_lock<std::mutex> lk(m);

    if(pDb)
        sqlite3_interrupt(pDb.get());

    if(current_task)
        current_task->cancel = true;

    next_task = nullptr;
    cv.notify_all();
}

void RowLoader::stop ()
{
    cancel();
    std::unique_lock<std::mutex> lk(m);
    stop_requested = true;
    cv.notify_all();
}

bool RowLoader::readingData () const
{
    std::unique_lock<std::mutex> lk(m);
    return pDb != nullptr;
}

void RowLoader::waitUntilIdle () const
{
    if(row_counter.valid())
        row_counter.wait();
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [this](){ return stop_requested || (!current_task && !next_task); });
}

void RowLoader::run ()
{
    for(;;)
    {
        std::unique_lock<std::mutex> lk(m);
        current_task = nullptr;
        cv.notify_all();

        cv.wait(lk, [this](){ return stop_requested || next_task; });

        if(stop_requested)
            return;

        current_task = std::move(next_task);
        lk.unlock();

        process(*current_task);
    }
}

void RowLoader::process (Task & t)
{
    QString sLimitQuery;
    if(query.startsWith("PRAGMA", Qt::CaseInsensitive) || query.startsWith("EXPLAIN", Qt::CaseInsensitive) ||
        // With RETURNING keyword DELETE,INSERT,UPDATE can return rows
        // https://www.sqlite.org/lang_returning.html
        query.startsWith("DELETE", Qt::CaseInsensitive) || query.startsWith("INSERT", Qt::CaseInsensitive) ||
        query.startsWith("UPDATE", Qt::CaseInsensitive))
    {
        sLimitQuery = query;
    } else {
        // Remove trailing trailing semicolon
        QString queryTemp = rtrimChar(query, ';');

        // If the query ends with a LIMIT statement or contains a compound operator take it as it is,
        // if not append our own LIMIT part for lazy population. The compound operator test is a very
        // weak check and does not detect whether the keyword is in a string or similar. This means
        // that lazy population is disabled for more queries than necessary. We should fix this once
        // we have a parser for SELECT statements but until then it is better to disable lazy population
        // for more statements than required instead of failing to run some statements entirely.

        if(queryTemp.contains(QRegularExpression("LIMIT\\s+.+\\s*((,|\\b(OFFSET)\\b)\\s*.+\\s*)?$", QRegularExpression::CaseInsensitiveOption)) ||
                queryTemp.contains(QRegularExpression("\\s(UNION)|(INTERSECT)|(EXCEPT)\\s", QRegularExpression::CaseInsensitiveOption)))
            sLimitQuery = queryTemp;
        else
            sLimitQuery = queryTemp + QString(" LIMIT %1 OFFSET %2;").arg(t.row_end-t.row_begin).arg(t.row_begin);
    }
    statement_logger(sLimitQuery);

    QByteArray utf8Query = sLimitQuery.toUtf8();
    sqlite3_stmt *stmt;
    auto row = t.row_begin;
    if(sqlite3_prepare_v2(pDb.get(), utf8Query, utf8Query.size(), &stmt, nullptr) == SQLITE_OK)
    {
        while(!t.cancel && sqlite3_step(stmt) == SQLITE_ROW)
        {
            size_t num_columns = static_cast<size_t>(sqlite3_data_count(stmt));

            // Construct a new row object with the right number of columns
            // 
            //Cache::value_type rowdata(num_columns);
            cacheWritter->startRow(num_columns);

            for(size_t i=0;i<num_columns;++i)
            {
                // No need to do anything for NULL values because we can just use the already default constructed value
                //auto type = sqlite3_column_type(stmt, static_cast<int>(i));
                //if(type != SQLITE_NULL)
                //{
                //    int bytes = sqlite3_column_bytes(stmt, static_cast<int>(i));
                //    if (bytes)
                //        if (!useTextCache_UTF8)
                //            rowdata[i] = QByteArray(static_cast<const char*>(sqlite3_column_blob(stmt, static_cast<int>(i))), bytes);
                //        else
                //            rowdata[i] = columnToString(stmt, i, type).toUtf8();
                //    else
                //        rowdata[i] = "";
                //}
                cacheWritter->writeColumn(stmt, i);
            }
            //std::lock_guard<std::mutex> lk(cache_mutex);
            std::lock_guard<std::mutex> lk(cacheWritter->getLock());

            //cache_data.set(row++, std::move(rowdata));
            cacheWritter->writeRow(row++);
        }

        sqlite3_finalize(stmt);

        // Query the total row count if and only if:
        // - this is the first batch of data we load for this query
        // - we got exactly the number of rows back we queried (which indicates there might be more rows)
        // If there is no need to query the row count this means the number of rows we just got is the total row count.
        if(!first_chunk_loaded)
        {
            first_chunk_loaded = true;
            if(row == t.row_end)
                triggerRowCountDetermination(t.token);
            else
                emit rowCountComplete(t.token, static_cast<int>(row-t.row_begin));
        }
    }

    emit fetched(t.token, t.row_begin, row);
}

void RowLoader::DeafultCacheWritter::startRow(int num)
{
    current.resize(num);
}


void RowLoader::DeafultCacheWritter::writeRow(int row)
{
    cache.set(row, std::move(current));
    current = Cache::value_type();
}

std::mutex& RowLoader::DeafultCacheWritter::getLock()
{
    
    return mutex;
}

RowLoader::DeafultCacheWritter::DeafultCacheWritter(Cache& cache, std::mutex& mutex)
    : cache(cache), mutex(mutex)
{
}

void RowLoader::DeafultCacheWritter::writeColumn(sqlite3_stmt* stmt, int column)
{
    auto type = sqlite3_column_type(stmt, column);
    assert(type != SQLITE_NULL);

    int bytes = sqlite3_column_bytes(stmt, column);

    if (bytes) {
        current[column] = QByteArray(static_cast<const char*>(sqlite3_column_blob(stmt, column)), bytes);
    }
    else
    {
        current[column] = "";
    }

}
