#pragma once
#include"DiffDialog.h"
#include"DiffHelper.h"
#include <QMetaEnum>
//todo
/**
connect headChange signal

*/
class DiffDialog::DiffDataModel :public QAbstractTableModel {
	Q_OBJECT
public:
	
	DiffWorker* worker;


	DiffDisplayMode mode = showOldTableData;
	void setMode(DisplayMode mode) {
		beginResetModel();
		this->mode = mode;
		switch (mode)
		{
			case DiffDisplayMode::showOldTableData:
				break;
			case DiffDisplayMode::showCurrentTableData:
			case DiffDisplayMode::showCurrentTableWithDiffResult:
			{
				break;
			}
			case DisplayMode::showOnlyAdded:
				break;
			case DiffDisplayMode::showOnlyRemove:
				break;
			case DiffDisplayMode::max:
				break;
			default:
				break;
		}
		endResetModel();
		
	};
	void resetAndRefresh() {
		beginResetModel();
		endResetModel();
	}
public slots:

public:



	void doRecord() {
		this->worker->doRecord();
	};

	DiffDataModel(DiffWorker* worker, QObject* parent = nullptr) : QAbstractTableModel(parent) {
		this->worker = worker;
		connect(this->worker, &DiffWorker::newDataLoadDone, this, [this](long long f,long long t, QString state) {
			if (this->mode != showOldTableData) {
				long long columnNum = this->worker->getHead().size();
				emit this->dataChanged(this->createIndex(f, 0), this->createIndex(t, columnNum));
				resetAndRefresh();
			}
			
		},Qt::QueuedConnection);

		connect(this->worker, &DiffWorker::recordOldDone, this, [this](QString state) {
			if (state == DiffWorker::workOK) {
				if (this->mode == showOldTableData) {
					this->resetAndRefresh();
				}
				
			}
		});
	}

	int rowCount(const QModelIndex& parent) const override
	{
		switch (mode)
		{
			case DiffDisplayMode::showOldTableData:
				return worker->getOldTable()->table.size();
				break;
			case DiffDisplayMode::showCurrentTableData:
			{
				return worker->getRowCount();
				
			}
				break;
			case DiffDisplayMode::showCurrentTableWithDiffResult:
				break;
			case DiffDisplayMode::showOnlyAdded:
				break;
			case DiffDisplayMode::showOnlyRemove:
				break;
			case DiffDisplayMode::max:
				break;
			default:
				break;
		}
	}
	int columnCount(const QModelIndex& parent) const override
	{

		switch (mode)
		{
			case DiffDisplayMode::showOldTableData:
			{
				auto t = worker->getOldTable();
				if (!t->table.isEmpty())return t->table[0].size();
			}
				break;
			case DiffDisplayMode::showCurrentTableData:
			{
				return worker->getHead().size();
			}
				break;
			case DiffDisplayMode::showCurrentTableWithDiffResult:
				break;
			case DiffDisplayMode::showOnlyAdded:
				break;
			case DiffDisplayMode::showOnlyRemove:
				break;
			case DiffDisplayMode::max:
				break;
			default:
				break;
		}
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (role == Qt::DisplayRole) {
			QString result = "at row %1 column %2";
			result = result.arg(index.row()).arg(index.column());
			switch (mode)
			{
				case DiffDisplayMode::showOldTableData:
					result = worker->getOldTable()->table.at(index.row()).at(index.column());
					break;
				case DiffDisplayMode::showCurrentTableData:
				{
					auto data = worker->getColumn(index.row(), index.column());
					if (data.has_value()) {
						result = data.value();
					}
					else {
						result = "waiting fro task";
					}
					break;
				}
				case DiffDisplayMode::showCurrentTableWithDiffResult:
					break;
				case DiffDisplayMode::showOnlyAdded:
					break;
				case DiffDisplayMode::showOnlyRemove:
					break;
				case DiffDisplayMode::max:
					break;
				default:
					break;
			}
			return result;
		}
		
		return QVariant();
	}
};