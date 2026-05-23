#pragma once

#include <QDialog>
#include <memory>

#include "ui_OBSUpdate.h"

class OBSUpdate : public QDialog {
	Q_OBJECT

public:
	enum ReturnVal { No, Yes, Skip };

	OBSUpdate(QWidget *parent, bool manualUpdate, const QString &text,
		bool* auto_update_enabled, bool* skip_update_once, bool* skip_version, bool* update_settings_changed);

public slots:
	void on_yes_clicked();
	void on_no_clicked();
	void on_skip_clicked();
	virtual void accept() override;
	virtual void reject() override;

private:
	std::unique_ptr<Ui_OBSUpdate> ui;
	bool* auto_update_enabled;
	bool* skip_update_once;
	bool* skip_version;
	bool* update_settings_changed;
};
