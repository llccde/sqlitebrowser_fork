#pragma once
#include"DiffDialog.h"
#include"DiffHelper.h"
#include <QMetaEnum>

class DiffDialog::DiffDataModel :public QAbstractTableModel {
	Q_OBJECT
public:
	
	DiffWorker* worker;

	enum DisplayMode {
		showOldTableData = 0,
		showCurrentTableData = 1,
		showCurrentTableWithDiffResult = 2,
		showOnlyAdded = 3,
		showOnlyRemove = 4,
		max = 5
	};
	Q_ENUM(DisplayMode);
	DisplayMode mode;
	void setMode(DisplayMode mode) {
		beginResetModel();
		this->mode = mode;
		endResetModel();
		
	};
	void resetAndRefresh() {
		beginResetModel();
		endResetModel();
	}
	
	DiffDataModel(DiffWorker* worker, QObject* parent = nullptr) : QAbstractTableModel(parent) {
		this->worker = worker;
	}
	int rowCount(const QModelIndex& parent) const override
	{
		auto table = worker->getOldTable()->table;
		if (table.isEmpty()) return 0;
		return table.size();
	}
	int columnCount(const QModelIndex& parent) const override
	{

		auto table = worker->getOldTable()->table;
		if (table.isEmpty()) return 0;
		//需要改进为使用head字段
		return table[0].size();
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (role == Qt::DisplayRole) {
			QString result = "at row %1 column %2";
			result = result.arg(index.row()).arg(index.column());
			switch (mode)
			{
			case DiffDialog::DiffDataModel::showOldTableData:
				result = worker->getOldTable()->table.at(index.row()).at(index.column());
				break;
			case DiffDialog::DiffDataModel::showCurrentTableData:
				break;
			case DiffDialog::DiffDataModel::showCurrentTableWithDiffResult:
				break;
			case DiffDialog::DiffDataModel::showOnlyAdded:
				break;
			case DiffDialog::DiffDataModel::showOnlyRemove:
				break;
			case DiffDialog::DiffDataModel::max:
				break;
			default:
				break;
			}
			return result;
		}
		
		return QVariant();
	}
};