#pragma once

#include <QDialog>
#include "ui_DiffDialog.h"

#include<memory>
QT_BEGIN_NAMESPACE
namespace Ui { class DiffDialog; };
QT_END_NAMESPACE

class MainWindow;
class DiffWorker;

class DiffDialog : public QDialog
{
	class DiffDataModel;

	Q_OBJECT
public:
	enum DisplayMode {
		showOldTableData = 0,
		showCurrentTableData = 1,
		showCurrentTableWithDiffResult = 2,
		showOnlyAdded = 3,
		showOnlyRemove = 4,
		max = 5
	};
	
	Q_ENUM(DisplayMode);
	bool DBChanged_ButNotInRealtimeMode_DidNotUpdateCache = false;
	long long currentIndex = 0;
	DisplayMode currentMode = showOldTableData;
	MainWindow* _mainWinow = nullptr;
	std::unique_ptr<DiffWorker> diffWorker = nullptr;
	std::unique_ptr<DiffDataModel> model = nullptr;
public:
	DiffDialog(MainWindow* mainWindow,QWidget *parent = nullptr);
	~DiffDialog();
signals:
	void startRecord();
	void startDiff();
	void recordDone();
	void diffDone();
public slots:
	void reloadTables();
private:
	Ui::DiffDialog *ui;
};
using DiffDisplayMode = DiffDialog::DisplayMode;
