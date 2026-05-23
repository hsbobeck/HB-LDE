#include "mac-update.hpp"

#include <obs-app.hpp>

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

// ----------------------------------------------------------------------------------------------------

@interface LdeUpdaterDelegate : NSObject <SPUUpdaterDelegate>
@property (nonatomic) SPUStandardUpdaterController *updaterController;
@end

@implementation LdeUpdaterDelegate

- (void)dealloc
{
    @autoreleasepool {
        [_updaterController.updater removeObserver:self forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))];
    }
}

- (void)observeCanCheckForUpdatesWithAction:(QAction *)action
{
    [_updaterController.updater addObserver:self
                                 forKeyPath:NSStringFromSelector(@selector(canCheckForUpdates))
                                    options:(NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew)
                                    context:(void *)action];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey, id> *)change context:(void *)context
{
    if ([keyPath isEqualToString:NSStringFromSelector(@selector(canCheckForUpdates))]) {
        QAction *menuAction = (QAction *)context;
        menuAction->setEnabled(_updaterController.updater.canCheckForUpdates);
    } else {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    }
}

@end

// ----------------------------------------------------------------------------------------------------

LdeUpdater::LdeUpdater(QMenuBar* menuBar)
{
    @autoreleasepool {
        _updaterDelegate = [[LdeUpdaterDelegate alloc] init];
        _updaterDelegate.updaterController = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES updaterDelegate:_updaterDelegate userDriverDelegate:nil];

        QMenu *updaterMenu = menuBar->addMenu("&Updater");

        QAction *checkForUpdatesAction = new QAction(QTStr("Check for Updates…"), this);
        checkForUpdatesAction->setMenuRole(QAction::AboutQtRole);
        updaterMenu->addAction(checkForUpdatesAction);

        _checkAutomaticallyAction = new QAction(QTStr("Check Automatically"), this);
        _checkAutomaticallyAction->setMenuRole(QAction::ApplicationSpecificRole);
        updaterMenu->addAction(_checkAutomaticallyAction);
        _checkAutomaticallyAction->setCheckable(true);

        auto appNativeMenu = [NSApp.mainMenu itemAtIndex:0].submenu;
        //
        auto checkForUpdatesNativeTitle = checkForUpdatesAction->text().toNSString();
        auto checkAutomaticallyNativeTitle = _checkAutomaticallyAction->text().toNSString();
        //
        NSMenuItem* checkForUpdatesNativeItemQ = nil;
        NSMenuItem* checkAutomaticallyNativeItemQ = nil;
        //
        for (NSMenuItem* nativeItem in appNativeMenu.itemArray) {
            if (!checkForUpdatesNativeItemQ) {
                if ([nativeItem.title isEqualToString:checkForUpdatesNativeTitle]) {
                    checkForUpdatesNativeItemQ = nativeItem;
                }
            }

            if (!checkAutomaticallyNativeItemQ) {
                if ([nativeItem.title isEqualToString:checkAutomaticallyNativeTitle]) {
                    checkAutomaticallyNativeItemQ = nativeItem;
                }
            }
        }
        //
        do {
            if (!(checkForUpdatesNativeItemQ && checkAutomaticallyNativeItemQ)) {
                break;
            }

            auto checkForUpdatesIndex = [appNativeMenu indexOfItem:checkForUpdatesNativeItemQ];
            auto checkAutomaticallyOldIndex = [appNativeMenu indexOfItem:checkAutomaticallyNativeItemQ];
            //
            if (!(checkForUpdatesIndex < checkAutomaticallyOldIndex)) {
                break;
            }

            auto checkAutomaticallyNewIndex = checkForUpdatesIndex + 1;
            //
            [appNativeMenu removeItemAtIndex:checkAutomaticallyOldIndex];
            [appNativeMenu insertItem:checkAutomaticallyNativeItemQ atIndex:checkAutomaticallyNewIndex];
        } while (false);

        connect(checkForUpdatesAction, &QAction::triggered, this, &LdeUpdater::handleCheckForUpdates);
        connect(_checkAutomaticallyAction, &QAction::triggered, this, &LdeUpdater::toggleCheckAutomatically);

        [_updaterDelegate observeCanCheckForUpdatesWithAction:checkForUpdatesAction];
        updateCheckAutomaticallyChecked();
    }
}

LdeUpdater::~LdeUpdater()
{
    delete _checkAutomaticallyAction;
}

auto LdeUpdater::updateCheckAutomaticallyChecked() -> void
{
    auto updater = _updaterDelegate.updaterController.updater;
    bool checked = updater.automaticallyChecksForUpdates;
    _checkAutomaticallyAction->setChecked(checked);
}

auto LdeUpdater::handleCheckForUpdates() -> void
{
    @autoreleasepool {
        [_updaterDelegate.updaterController checkForUpdates:nil];
    }
}

auto LdeUpdater::toggleCheckAutomatically() -> void
{
    @autoreleasepool {
        auto updater = _updaterDelegate.updaterController.updater;
        updater.automaticallyChecksForUpdates = !updater.automaticallyChecksForUpdates;
        updateCheckAutomaticallyChecked();
    }
}
