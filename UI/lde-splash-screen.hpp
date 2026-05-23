#pragma once

#include <QLabel>
#include <QPointer>
#include <QSplashScreen>

class LdeSplashScreen : public QSplashScreen {
private:
    QPointer<QLabel> _label{};

public:
    explicit LdeSplashScreen(QScreen* screenQ, const QPixmap& pixmap);
};
