#pragma once
#include<QAbstractItemModel>
#include<QVector>
#include<memory>
#include<optional>
#include<atomic>
#include<Qmap>
#include<mutex>
#include<sqlite3.h>
#include"sqlitedb.h"
#include"qthread.h"
#include"qobject.h"
struct sqlite3;
class SqliteTableModel;

class DiffWorker:public QObject{
	Q_OBJECT
	std::function<std::shared_ptr<sqlite3>()> db_getter;
	DBBrowserDB* _db;
	QString selectQuery;
	QString countQuery;
	QThread thread;
public:
	enum columnType {
		intType = SQLITE_INTEGER,
		floatType = SQLITE_FLOAT,
		textType = SQLITE_TEXT,
		byteType = SQLITE_BLOB,
		unknownType = SQLITE_FLOAT
	};
	static inline int toSqlite(columnType t) { return t;};

	struct OldTable {
		bool hasLimit = false;
		long long limitRowNum = -1;
		bool rowNumTotally = 1;

		QVector<QVector<QString>> table;
		QVector<int> primary;

		struct column{
			QString name;
			columnType type;
		};
		QVector<column> head;
		inline void clear() {
		};
		inline OldTable(bool hasLimit, long long limitRowNum, bool rowNumTotally, const QVector<QVector<QString>>& table, const QVector<column> head)
			: hasLimit(hasLimit), limitRowNum(limitRowNum), rowNumTotally(rowNumTotally), table(table), head(head){}
		inline OldTable(){};
	};
	struct DiffResult {
		QVector<int> primary;
		QVector<QString> head;
		QVector<int> changedRowIncurrent;
		QVector<int> addedRowInCurrent;
		QVector<int> removedRowInOld;
	};
	using WorkID = long long int;
	std::mutex swapCache;

	std::mutex prepareWorking;
	std::atomic<WorkID> workID;
	
	std::unique_ptr<OldTable> oldTable;
	std::unique_ptr<DiffResult> diffResult;

	QString tableName;
	DiffWorker(DBBrowserDB* _db);
	void setTable(const QString& tableName);

	//ÒÔprimaryColumnÎªË÷Òý
	
	QVector<int> getAddedRowIncurrent();
	QVector<int> getRemovedRowInOld();
	
	QVector<int> getChangedRowInCurrent();
	QVector<int> getChangedRowInOld();
	//-----------------------------

	QVector<bool> isRowInCurrentNew();
	QVector<bool> isRowInOldRemoved();
	

	QVector<bool> isRowInCurrentChanged();
	QVector<bool> isRowInOldChanged();
	//----------------------------

	bool isRowInCurrentNew(int row);
	bool isRowinOldRemoved(int row);

	bool isRowInCurrentChanged(int row);
	bool isRowInOldChanged(int row);
	//----------------------------
	
	QVector<bool> isColumnChanged_current(int rowInCurrent);
	QVector<bool> isColumnChanged_old(int rowInOld);
	WorkID inline getWorkID() {
		std::lock_guard l(prepareWorking);
		workID++;
		if (workID == -1);
		workID = 0;
		return workID;
	};
	inline void stop() {
		workID = -1;
	}
	inline void launch() {
		assert(!thread.isRunning());
		thread.start();
	};
	inline void doRecord() {
		emit startRecord(getWorkID());
	};
	inline void doDiff() {
		emit startDiff(getWorkID());
	};
	~DiffWorker() {
		stop();
		thread.quit();
		thread.wait();
	}
signals:
	void recordOldDone();
	void diffDone();

	void startRecord(WorkID ID);
	void startDiff(WorkID ID);
public slots:
	void recordOld(WorkID ID);
	void diff(WorkID ID);

};
