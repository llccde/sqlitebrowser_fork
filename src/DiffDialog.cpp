#include "DiffDialog.h"
#include <qabstractitemmodel.h>
#include<MainWindow.h>
#include"DiffHelper.h"
class MyModel :public QAbstractTableModel{
	
public:
	DiffWorker* worker;
	MyModel(DiffWorker* worker,QObject* parent = nullptr) :worker(worker), QAbstractTableModel(parent) {
	}
	int rowCount(const QModelIndex& parent) const override
	{
		return 2;
	}
	int columnCount(const QModelIndex& parent) const override
	{
		return 3;
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (role == Qt::DisplayRole)
			return QString("Row%1, Column%2")
			.arg(index.row() + 1)
			.arg(index.column() + 1);

		return QVariant();
	}
};
DiffDialog::DiffDialog(MainWindow* mainWindow, QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::DiffDialog())
	, _mainWinow(mainWindow)
{
	ui->setupUi(this);
	auto db = &(this->_mainWinow->getDb());

	this->diffWorker.reset(new DiffWorker(db));

	ui->dataView->setModel(new MyModel(this->worker.get(), this));

	connect(db, &DBBrowserDB::dbChanged, this, [this](bool dirt) {
		reloadTables();});
	connect(db, &DBBrowserDB::structureUpdated, this, [this]() {
		reloadTables();});
	
	assert(worker != nullptr);
	connect(ui->recordOldButton, &QPushButton::clicked, this, [this](){
		this->worker->doRecord();
		});
	connect(ui->diffButton, &QPushButton::clicked, this, [this]() {
		this->worker->doDiff();
		});


	auto worker = diffWorker.get();
	connect(worker, &DiffWorker::startRecord, this, [this](DiffWorker::WorkID id) {emit startRecord();});
	connect(worker, &DiffWorker::startDiff, this, [this](DiffWorker::WorkID id) {emit startDiff();});

	connect(worker, &DiffWorker::recordOldDone, this, [this]() {emit recordDone();});
	connect(worker, &DiffWorker::diffDone, this, [this]() {emit diffDone();});

	
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
