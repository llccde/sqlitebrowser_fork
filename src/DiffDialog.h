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
	Q_OBJECT
		std::unique_ptr<DiffWorker> worker;
	MainWindow* _mainWinow = nullptr;
	std::unique_ptr<DiffWorker> diffWorker = nullptr;
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

