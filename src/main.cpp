#include "MainWindow.h"
#include "OpenALSoundPlayer.h"
#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Feedra"));
    QApplication::setOrganizationName(QStringLiteral("Feedra"));

    // Fusion looks the same on every platform and takes its colours from the theme palette.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NewMediaFett.ttf"));
    const QStringList uiFonts{
        QStringLiteral(":/fonts/Geist-Regular.ttf"),
        QStringLiteral(":/fonts/Geist-Medium.ttf"),
        QStringLiteral(":/fonts/Geist-SemiBold.ttf"),
        QStringLiteral(":/fonts/GeistMono-Regular.ttf"),
        QStringLiteral(":/fonts/GeistMono-Medium.ttf"),
    };
    for (const QString& font : uiFonts) {
        QFontDatabase::addApplicationFont(font);
    }
    QFont uiFont(QStringLiteral("Geist"));
    uiFont.setPointSizeF(10.0);
    uiFont.setStyleStrategy(QFont::PreferAntialias);
    QApplication::setFont(uiFont);

    Theme::instance().apply();
    app.setWindowIcon(QIcon(QStringLiteral(":/images/feedra.png")));

    MainWindow window;
    window.show();
    const int code = app.exec();
    OpenALSoundPlayer::close();
    return code;
}
