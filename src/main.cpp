#include "LogCapture.h"
#include "MainWindow.h"
#include "AudioDevice.h"
#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    // First, so everything written to the terminal is also kept for View > Logs.
    LogCapture::install();
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

    int code = 0;
    {
        // Scoped so the window, and every pad's player with it, is gone before the device
        // closes: players delete their sources while the context is still current.
        MainWindow window;
        window.show();
        code = app.exec();
    }
    AudioDevice::close();
    return code;
}
