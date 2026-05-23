#pragma once

#include <QMenuBar>
#include <QObject>

#ifdef __OBJC__
@class LdeUpdaterDelegate;
#endif

class LdeUpdater : public QObject
{
    Q_OBJECT

private:
#ifdef __OBJC__
    LdeUpdaterDelegate *_updaterDelegate{};
#else
    void *_updaterDelegate{};
#endif
    QAction *_checkAutomaticallyAction{};

public:
    LdeUpdater(QMenuBar* menuBar);
    ~LdeUpdater();

private:
    auto updateCheckAutomaticallyChecked() -> void;

private slots:
    auto handleCheckForUpdates() -> void;
    auto toggleCheckAutomatically() -> void;
};
