#pragma once
#include<QAbstractItemModel>
#include<QVector>
#include<memory>
#include<optional>
#include<atomic>
#include<Qmap>
#include<mutex>
#include<sqlite.h>
#include"RowCache.h"
#include"qthread.h"
#include"qobject.h"
#include <concepts>
#include <cstddef>




class RowLoader;
struct sqlite3;
class SqliteTableModel;
class DBBrowserDB;

class DiffWorker:public QObject{
	Q_OBJECT
private:
	std::function<std::shared_ptr<sqlite3>()> db_getter;
	DBBrowserDB* _db;
	QString selectQuery;
	QString countQuery;
	QThread thread;
	std::atomic<int> currentRowIndex = 0;
	std::atomic<int> chunkSize = 1000;
	std::unique_ptr<RowLoader> rowLoader;
public:
	class My_Key;
	
	static const QString workOK ;
	
	enum columnType {
		intType = SQLITE_INTEGER,
		floatType = SQLITE_FLOAT,
		textType = SQLITE_TEXT,
		byteType = SQLITE_BLOB,
		unknownType = SQLITE_FLOAT
	};
	static inline int toSqliteType(columnType t) { return t;};
	struct column {
		QString name;
		columnType type;

		column(const QString& name, const columnType& type)
			: name(name), type(type)
		{
		}
	};
	QVector<column> head;
	inline QVector<column>& getHead() {
		return head;
	};
	struct OldTable {
		bool hasLimit = false;
		long long limitRowNum = -1;
		bool rowNumTotally = 1;

		QVector<QVector<QString>> table;
		QVector<int> primary;


		inline void clear() {
		};
		inline OldTable(bool hasLimit, long long limitRowNum, bool rowNumTotally, const QVector<QVector<QString>>& table, const QVector<column> head)
			: hasLimit(hasLimit), limitRowNum(limitRowNum), rowNumTotally(rowNumTotally), table(table){}
		inline OldTable(){};
	};
	using rowInOldTable = int;
	QMap<My_Key, rowInOldTable> oldTableIndex;
	
	

	using WorkID = long long int;
	using RowLoaderWorkID = long long int;
	//只作为参数使用,反正rowloader中也没有实际使用
	std::vector<std::string> headers;

	
	std::mutex CacheMutex;

	Cache_T<QString> my_cache;
	QVector<int> primary;

	std::atomic<WorkID> workID;
	std::atomic<RowLoaderWorkID> rowLoaderWorkID;

	std::mutex swapOldTablePtr;
	std::unique_ptr<OldTable> oldTable;

	std::mutex swapDiffResultPtr;

	QString tableName;
	DiffWorker(DBBrowserDB* _db);
	void setTable(const QString& tableName);
	void loadHead(bool callInInitLine = false);
	void refreshCurrentTable();

	//以primaryColumn为索引
	inline const OldTable* getOldTable() {
		return oldTable.get();
	}

	const QVector<int>* getAddedRowIncurrent();
	const QVector<int>* getRemovedRowInOld();
	
	const QVector<int>* getChangedRowInCurrent();
	const QVector<int>* getChangedRowInOld();
	//-----------------------------

	const QVector<bool>* isRowInCurrentNew();
	const QVector<bool>* isRowInOldRemoved();
	

	const QVector<bool>* isRowInCurrentChanged();
	const QVector<bool>* isRowInOldChanged();
	//----------------------------
	inline std::optional<QString&> getColumn(long long row, long long column) {

		if (my_cache.count(row)) {
			auto t = my_cache.at(row);
			bool inRange = column < t.size();
			assert(inRange);
			if (inRange) {
				return t.at(column);
			}
			else {
				return std::nullopt;
			}
		}
		else {
			return std::nullopt;
		}
	};
	bool isRowInCurrent_New(int row);
	bool isRowinOld_Removed(int row);

	bool isRowInCurrent_Changed(int row);
	bool isRowInOld_Changed(int row);
	//----------------------------
	
	QVector<bool> isColumnInCurrent_Changed(int rowInCurrent);
	QVector<bool> isColumnOld_Changed(int rowInOld);
	WorkID inline getWorkID() {
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
	inline int columnNum() {
		return head.size();
	}
	int getRowCount();
	~DiffWorker();
signals:
	void recordOldDone(QString state = workOK);
	void diffDone(QString state = workOK);

	void dataReady(long long begin, long long end);

	void startRecord(WorkID ID);
	void startDiff(WorkID ID);
	
	void newDataLoadDone(long long from,long long to, QString state = workOK);
	void currentTableHeadChanged();
public: 
public slots:

	void recordOld(WorkID ID);
	void diff(WorkID ID);
	void loadData(long long row);

public:
	class My_Key {
	public:
		virtual int getPrimaryNum() = 0;
		virtual QString& getPrimary(int n) = 0;
		bool operator>(My_Key& another) {
			assert(this->getPrimaryNum() == another.getPrimaryNum());
			for (int i = 0; i < getPrimaryNum(); i++) {
				QString& a = getPrimary(i);
				QString& b = another.getPrimary(i);
				if (a > b) {
					return true;
				}
				else if (b > a) {
					return false;
				}
				continue;
			}
			return false;
		}
	};
	class OldTableKey :public My_Key {

		int index;
		DiffWorker* outer;
	public:

		// 通过 My_Key 继承
		int getPrimaryNum() override {
			assert(outer != nullptr);
			return outer->oldTable->primary.size();
		};

		QString& getPrimary(int n) override {

			return outer->oldTable->table[index][
				outer->oldTable->primary[n]
			];
		};
		OldTableKey(int index_, DiffWorker* out_) :outer(out_), index(index_) {};

	};
	class CurrentTableKey :public My_Key {
		int rowIndex;
		DiffWorker* outer;
	public:
		int getPrimaryNum() override{
			return outer->primary.size();
		}
		QString& getPrimary(int n) override {
			static QString empty = "";
			auto t = outer->getColumn(rowIndex, outer->primary[n]);
			if (t.has_value()) {
				return t.value();
			}
			else{
				return empty;
			}
		};
	};

};

