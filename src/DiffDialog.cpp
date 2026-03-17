#include "DiffDialog.h"
#include <qabstractitemmodel.h>
#include<MainWindow.h>
#include"DiffHelper.h"
#include"DiffDataModel.h"
#include"qobject.h"
#include <QMetaEnum>
#include<qdatetime.h>
DiffDialog::DiffDialog(MainWindow* mainWindow, QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::DiffDialog())
	, _mainWinow(mainWindow)
{
	ui->setupUi(this);
	auto db = &(this->_mainWinow->getDb());

	this->diffWorker.reset(new DiffWorker(db));
	this->model.reset(new DiffDataModel(this->diffWorker.get(), this));

	ui->dataView->setModel(model.get());

	QMetaEnum m = QMetaEnum::fromType<DisplayMode>();
	assert(m.isValid());
	for (int i = 0;i < DisplayMode::max;i++) {
		QString s = m.valueToKey(i);
		ui->displayMode->addItem(s);
	}
	//当显示模式改变时
	connect(ui->displayMode, &QComboBox::currentIndexChanged, this, [this](int index) {
		this->currentMode = static_cast<DisplayMode>(index);
		if (currentMode != showOldTableData) {
			if (DBChanged_ButNotInRealtimeMode_DidNotUpdateCache) {
				this->diffWorker->refreshCurrentTable();
			}
		}
		this->model->setMode(static_cast<DisplayMode>(index));
	});


	//当数据库结构变化

	connect(db, &DBBrowserDB::dbChanged, this, [this](bool dirt) {
		reloadTables();
		//更新缓存
		if (currentMode != showOldTableData) {
			diffWorker.get()->refreshCurrentTable();
		}else{
			DBChanged_ButNotInRealtimeMode_DidNotUpdateCache = true;
		}
	});
	connect(db, &DBBrowserDB::structureUpdated, this, [this]() {
		reloadTables();
		//更新缓存
		if (currentMode != showOldTableData) {
			diffWorker.get()->refreshCurrentTable();
		}else{
			DBChanged_ButNotInRealtimeMode_DidNotUpdateCache = true;
		}
	});

	/**
	这里设计不太优雅,需要改current模式下的加载更新逻辑
	*/

	//当选择了新的表
	connect(ui->tableSelect, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index == -1) return;
		QString tableName = this->ui->tableSelect->itemData(index, Qt::DisplayRole).toString();
		this->diffWorker->setTable(tableName);
		if (currentMode != showOldTableData) {
			//将会自动触发视图的更新
			diffWorker.get()->refreshCurrentTable();
		}
		else {
			model->resetAndRefresh();
		}
		});
	
	assert(diffWorker != nullptr);
	//点击记录表按钮
	connect(ui->recordOldButton, &QPushButton::clicked, this, [this](){
		this->diffWorker->doRecord();
		});
	connect(ui->diffButton, &QPushButton::clicked, this, [this]() {
		this->diffWorker->doDiff();
		});


	auto worker = diffWorker.get();
	//开始工作
	connect(worker, &DiffWorker::startRecord, this, [this](DiffWorker::WorkID id) {
		ui->stateLabel->setText("start record the table at:"+QDateTime::currentDateTime().toString("HH:mm:ss"));
		emit startRecord();
		},Qt::QueuedConnection);
	connect(worker, &DiffWorker::startDiff, this, [this](DiffWorker::WorkID id) {
	
		emit startDiff();
		}, Qt::QueuedConnection);
	//结束工作
	connect(worker, &DiffWorker::recordOldDone, this, [this](QString state) {
		emit recordDone();
		ui->stateLabel->setText("record "+state+" at:" + QDateTime::currentDateTime().toString("HH:mm:ss"));
		}, Qt::QueuedConnection);
	connect(worker, &DiffWorker::diffDone, this, [this](QString state) {emit diffDone();}, Qt::QueuedConnection);

	
}

DiffDialog::~DiffDialog()
{
	delete ui;
}
void DiffDialog::reloadTables() {
	//加载所有表项
	int index = ui->tableSelect->currentIndex();
	QString name = ui->tableSelect->itemData(index,Qt::DisplayRole).toString();
	ui->tableSelect->clear();
	auto schem = &(_mainWinow->getDb().schemata);
	auto db = schem->find("main");
	if (db == schem->end()) {
		return;
	}
	auto allTable = (*db).second.tables;
	
	int currentSelectIndexAfterUpdate = -1;

	for (auto i:allTable)
	{
		auto tableName = QString::fromStdString((i.second.get()->name()));
		if (tableName == name) {
			currentSelectIndexAfterUpdate = ui->tableSelect->count();
		}
		ui->tableSelect->addItem(tableName);
	}

	if (currentSelectIndexAfterUpdate != -1) {
		ui->tableSelect->setCurrentIndex(currentSelectIndexAfterUpdate);
	}

}
