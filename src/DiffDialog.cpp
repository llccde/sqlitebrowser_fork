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

	QMetaEnum m = QMetaEnum::fromType<DiffDataModel::DisplayMode>();
	assert(m.isValid());
	for (int i = 0;i < DiffDataModel::DisplayMode::max;i++) {
		QString s = m.valueToKey(i);
		ui->displayMode->addItem(s);
	}
	connect(ui->displayMode, &QComboBox::currentIndexChanged, this, [this](int index) {
		this->model->setMode(static_cast<DiffDataModel::DisplayMode>(index));

		});



	connect(db, &DBBrowserDB::dbChanged, this, [this](bool dirt) {
		reloadTables();});
	connect(db, &DBBrowserDB::structureUpdated, this, [this]() {
		reloadTables();});

	connect(ui->tableSelect, &QComboBox::currentIndexChanged, this, [this](int index) {
		QString tableName = this->ui->tableSelect->itemData(index, Qt::DisplayRole).toString();
		this->diffWorker->setTable(tableName);
		});
	
	assert(diffWorker != nullptr);
	connect(ui->recordOldButton, &QPushButton::clicked, this, [this](){
		this->diffWorker->doRecord();
		});
	connect(ui->diffButton, &QPushButton::clicked, this, [this]() {
		this->diffWorker->doDiff();
		});


	auto worker = diffWorker.get();
	connect(worker, &DiffWorker::startRecord, this, [this](DiffWorker::WorkID id) {
		ui->stateLabel->setText("start record the table at:"+QDateTime::currentDateTime().toString("HH:mm:ss"));
		emit startRecord();
		},Qt::QueuedConnection);
	connect(worker, &DiffWorker::startDiff, this, [this](DiffWorker::WorkID id) {
		
		emit startDiff();
		}, Qt::QueuedConnection);

	connect(worker, &DiffWorker::recordOldDone, this, [this](QString state) {
		emit recordDone();
		ui->stateLabel->setText("record "+state+" at:" + QDateTime::currentDateTime().toString("HH:mm:ss"));
		if (state == DiffWorker::workOK) {
			this->model->resetAndRefresh();
		}
		});
	connect(worker, &DiffWorker::diffDone, this, [this](QString state) {emit diffDone();});

	
}

DiffDialog::~DiffDialog()
{
	delete ui;
}
void DiffDialog::reloadTables() {
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
