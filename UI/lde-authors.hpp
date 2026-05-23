#pragma once

#include <memory>
#include <QDialog>

#include "ui_LdeAuthors.h"

class LdeAuthors : public QDialog {
	Q_OBJECT

public:
	explicit LdeAuthors(QWidget *parent = 0);

	std::unique_ptr<Ui::LdeAuthors> ui;
};
