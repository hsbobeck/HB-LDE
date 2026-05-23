#include "lde-license.hpp"

#include "obs-app.hpp"
#include "qt-wrappers.hpp"
#include <platform.hpp>

LdeLicense::LdeLicense(QWidget *parent) : QDialog(parent), ui(new Ui::LdeLicense)
{
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	ui->setupUi(this);

    std::string path;
    QString error = QTStr("About.Error").arg("https://github.com/obsproject/obs-studio/blob/master/COPYING");

    if (!GetDataFilePath("license/gplv2.txt", path)) {
        ui->textBrowser->setPlainText(error);
        return;
    }

    BPtr<char> text = os_quick_read_utf8_file(path.c_str());

    if (!text || !*text) {
        ui->textBrowser->setPlainText(error);
        return;
    }

    ui->textBrowser->setPlainText(QT_UTF8(text));
}
