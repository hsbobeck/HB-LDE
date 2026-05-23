#include "lde-splash-screen.hpp"

#include <sstream>
#include "qt-wrappers.hpp"

static auto MakeText(int iteration) -> QString
{
    std::ostringstream stream{};
    stream << "LDE v" << obs_get_version_string() << "\nInitializing";
    if (1 == iteration) {
        stream << ".";
    } else if (2 == iteration) {
        stream << "..";
    } else if (3 == iteration) {
        stream << "...";
    }
    return QString{stream.str().c_str()};
}

LdeSplashScreen::LdeSplashScreen(QScreen* screenQ, const QPixmap& pixmap)
    : QSplashScreen{screenQ, pixmap}
{
    setUpdatesEnabled(false);

    _label = new QLabel{"", this};
    _label->move(65, 150);

    QFont font = _label->font();
    font.setStyleStrategy(QFont::NoAntialias);
    _label->setFont(font);

#if defined(__APPLE__)
    _label->setStyleSheet("color: #808080; font-size: 14pt; font-family: \"Arial\"; line-height: 120%");
#elif defined(_WIN32)
    _label->setStyleSheet("color: #808080; font-size: 12pt; font-family: \"Arial\"; line-height: 120%");
#endif
    _label->setText(MakeText(3));
    _label->show();

    setUpdatesEnabled(true);
}
