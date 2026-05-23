#pragma once

#include <QMenuBar>
#include <QObject>

class LdeUpdater : public QObject
{
	Q_OBJECT

private:
	QAction* _checkAutomaticallyAction{};

public:
	LdeUpdater(QMenuBar* menuBar);
	~LdeUpdater();

private:
	auto updateCheckAutomaticallyChecked() -> void;

private slots:
	auto handleCheckForUpdates() -> void;
	auto toggleCheckAutomatically() -> void;

public slots:
	auto readConfigKeyValue(const char* key, wchar_t* buffer, std::size_t bufferWcharCount, int* resultPtr) -> void;
	auto writeConfigKeyValue(const char* key, const wchar_t* valueWchars) -> void;
	auto deleteConfigKeyValue(const char* key) -> void;
};
