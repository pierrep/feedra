#include "MainWindow.h"
#include "OpenALSoundPlayer.h"
#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Feedra"));
    QApplication::setOrganizationName(QStringLiteral("Feedra"));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NewMediaFett.ttf"));
    Theme::instance().apply();
    app.setWindowIcon(QIcon(QStringLiteral(":/images/feedra.png")));

    MainWindow window;
    window.show();
    const int code = app.exec();
    OpenALSoundPlayer::close();
    return code;
}
