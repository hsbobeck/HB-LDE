#include "update-window.hpp"
#include "obs-app.hpp"

#include <qdesktopservices.h>

OBSUpdate::OBSUpdate(QWidget *parent, bool manualUpdate, const QString &text,
	bool* auto_update_enabled, bool* skip_update_once, bool* skip_version, bool* update_settings_changed)
	: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint |
				  Qt::WindowCloseButtonHint),
	  ui(new Ui_OBSUpdate),
	auto_update_enabled(auto_update_enabled),
	skip_update_once(skip_update_once),
	skip_version(skip_version),
	update_settings_changed(update_settings_changed)
{
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	//QMetaObject::invokeMethod(this, "ToggleAlwaysOnTop",
	//	Qt::QueuedConnection);

	ui->setupUi(this);
	ui->text->setOpenExternalLinks(true);
	ui->text->setHtml(text);

	if (manualUpdate) {
		delete ui->skip;
		ui->skip = nullptr;

		ui->no->setText(QTStr("Cancel"));
	}
}

void OBSUpdate::on_yes_clicked()
{
	QDesktopServices::openUrl(QUrl("https://www.louper.io/lde-versions/"));
	done(OBSUpdate::Yes);
}

void OBSUpdate::on_no_clicked()
{
	*skip_update_once = true;
	*update_settings_changed = true;
	done(OBSUpdate::No);
}

void OBSUpdate::on_skip_clicked()
{
	*skip_version = true;
	*update_settings_changed = true;
	done(OBSUpdate::Skip);
}

void OBSUpdate::accept()
{
	done(OBSUpdate::Yes);
}

void OBSUpdate::reject()
{
	done(OBSUpdate::No);
}
