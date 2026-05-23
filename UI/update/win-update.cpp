#include "win-update.hpp"

#include <QThread>

#include <obs-app.hpp>
#include <winsparkle.h>

// NOTE: Set to 1 for debugging purposes, 0 otherwise.
#define iLOG_CONFIG 0

constexpr auto kConfigSection = "WinSparkle";

static auto __cdecl StaticReadConfigKeyValue(const char *key, wchar_t *buffer,
					     std::size_t bufferWcharCount,
					     void *context) -> int
{
	auto updater = static_cast<LdeUpdater*>(context);
	auto onGuiThread = (QCoreApplication::instance()->thread() == QThread::currentThread());
	int intResult{};

	if (onGuiThread) {
		updater->readConfigKeyValue(key, buffer, bufferWcharCount, &intResult);
	} else {
		QMetaObject::invokeMethod(updater, "readConfigKeyValue", Qt::BlockingQueuedConnection,
			Q_ARG(const char*, key),
			Q_ARG(wchar_t*, buffer),
			Q_ARG(std::size_t, bufferWcharCount),
			Q_ARG(int*, &intResult));
	}

	return intResult;
}

static auto __cdecl StaticWriteConfigKeyValue(const char *key,
					      const wchar_t * valueWchars,
					      void *context) -> void
{
	auto updater = static_cast<LdeUpdater*>(context);
	auto onGuiThread = (QCoreApplication::instance()->thread() == QThread::currentThread());

	if (onGuiThread) {
		updater->writeConfigKeyValue(key, valueWchars);
	} else {
		QMetaObject::invokeMethod(updater, "writeConfigKeyValue", Qt::BlockingQueuedConnection,
			Q_ARG(const char*, key),
			Q_ARG(const wchar_t*, valueWchars));
	}
}

static auto __cdecl StaticDeleteConfigKeyValue(const char *key, void *context) -> void
{
	auto updater = static_cast<LdeUpdater*>(context);
	auto onGuiThread = (QCoreApplication::instance()->thread() == QThread::currentThread());

	if (onGuiThread) {
		updater->deleteConfigKeyValue(key);
	} else {
		QMetaObject::invokeMethod(updater, "deleteConfigKeyValue", Qt::BlockingQueuedConnection,
			Q_ARG(const char*, key));
	}
}

static auto __cdecl StaticShutDown() -> void
{
	QCoreApplication::quit();
}

LdeUpdater::LdeUpdater(QMenuBar *menuBar)
{
	auto firstRun = (nullptr == config_get_string(App()->GlobalConfig(), kConfigSection, "DidRunOnce"));

	bool autoCheck{};
	if (firstRun) {
		autoCheck = true;
	} else {
		auto valueCStringQ = config_get_string(App()->GlobalConfig(), kConfigSection, "CheckForUpdates");
		if (valueCStringQ) {
			autoCheck = (0 == strcmp(valueCStringQ, "1"));
		} else {
			autoCheck = false;
		}
	};

	win_sparkle_config_methods_t configMethods{};
	configMethods.config_read = &StaticReadConfigKeyValue;
	configMethods.config_write = &StaticWriteConfigKeyValue;
	configMethods.config_delete = &StaticDeleteConfigKeyValue;
	configMethods.user_data = static_cast<void*>(this);
	win_sparkle_set_config_methods(&configMethods);
	//
	win_sparkle_set_shutdown_request_callback(&StaticShutDown);
	//
	win_sparkle_init();

	if (firstRun) {
		win_sparkle_set_automatic_check_for_updates(1);
	}

	QMenu *helpMenu{};
	//
	for (auto action : menuBar->actions()) {
		auto actionMenuQ = action->menu();
		if (actionMenuQ) {
			helpMenu = actionMenuQ;
		}
	}
	//
	int helpMenuItemCount = helpMenu->actions().size();
	int preAboutSeparatorIndex = helpMenuItemCount - 4;

	_checkAutomaticallyAction =
		new QAction(QTStr("Check Automatically"), this);
	helpMenu->insertAction(helpMenu->actions().at(preAboutSeparatorIndex),
			       _checkAutomaticallyAction);
	_checkAutomaticallyAction->setCheckable(true);
	_checkAutomaticallyAction->setChecked(autoCheck);

	QAction *checkForUpdatesAction =
		new QAction(QTStr("Check for Updates…"), this);
	helpMenu->insertAction(_checkAutomaticallyAction,
			       checkForUpdatesAction);

	helpMenu->insertSeparator(checkForUpdatesAction);

	connect(checkForUpdatesAction, &QAction::triggered, this,
		&LdeUpdater::handleCheckForUpdates);
	connect(_checkAutomaticallyAction, &QAction::triggered, this,
		&LdeUpdater::toggleCheckAutomatically);
}

LdeUpdater::~LdeUpdater()
{
	delete _checkAutomaticallyAction;
	win_sparkle_cleanup();
}

auto LdeUpdater::updateCheckAutomaticallyChecked() -> void
{
	bool checked = (win_sparkle_get_automatic_check_for_updates() != 0);
	_checkAutomaticallyAction->setChecked(checked);
}

auto LdeUpdater::handleCheckForUpdates() -> void
{
	win_sparkle_check_update_with_ui();
}

auto LdeUpdater::toggleCheckAutomatically() -> void
{
	bool check = (win_sparkle_get_automatic_check_for_updates() != 0);
	win_sparkle_set_automatic_check_for_updates(!check);
}

auto LdeUpdater::readConfigKeyValue(const char* key, wchar_t* buffer, std::size_t bufferWcharCount, int* resultPtr) -> void
{
	auto valueCStringQ = config_get_string(App()->GlobalConfig(), kConfigSection, key);
	if (!valueCStringQ) {
#if iLOG_CONFIG
		blog(LOG_INFO, "LdeUpdater: Read \"%s\" (bufferWcharCount: %llu): -", key, bufferWcharCount);
#endif
		*resultPtr = 0;
		return;
	}
#if iLOG_CONFIG
	blog(LOG_INFO, "LdeUpdater: Read \"%s\" (bufferWcharCount: %llu): \"%s\"", key, bufferWcharCount, valueCStringQ);
#endif
	auto valueString = QString{valueCStringQ};

	auto valueWstring = valueString.toStdWString();
	auto valueWcharCount = valueWstring.size();
	if (bufferWcharCount < valueWcharCount) {
		*resultPtr = 0;
		return;
	}

	auto valueWchars = valueWstring.data();
	std::copy(valueWchars, valueWchars + valueWcharCount, buffer);
	*resultPtr = 1;
}

auto LdeUpdater::writeConfigKeyValue(const char* key, const wchar_t* valueWchars) -> void
{
	auto valueString = QString::fromWCharArray(valueWchars);
	auto valueByteArray = valueString.toUtf8();
	auto valueCString = valueByteArray.constData();
	config_set_string(App()->GlobalConfig(), kConfigSection, key, valueCString);
#if iLOG_CONFIG
	blog(LOG_INFO, "LdeUpdater: Write \"%s\" : \"%s\"", key, valueCString);
#endif
}

auto LdeUpdater::deleteConfigKeyValue(const char* key) -> void
{
	// NOTE: We ignore the result of the following call.
	config_remove_value(App()->GlobalConfig(), kConfigSection, key);
	//
#if iLOG_CONFIG
	blog(LOG_INFO, "LdeUpdater: Delete \"%s\"", key);
#endif
}
