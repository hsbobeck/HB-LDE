#pragma once

#include <memory>
#include <QDialog>

#include "ui_LdeLicense.h"

class LdeLicense : public QDialog {
	Q_OBJECT

public:
	explicit LdeLicense(QWidget *parent = 0);

	std::unique_ptr<Ui::LdeLicense> ui;
};
