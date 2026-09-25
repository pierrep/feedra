#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QStandardPaths>

AppConfig::AppConfig() = default;

void AppConfig::setup()
{
    defaultLibraryLocation = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (defaultLibraryLocation.isEmpty()) {
        defaultLibraryLocation = QDir::homePath();
    }
    m_masterVolume = 1.0f;
    m_masterFade = 1.0f;
    loopByDefault = false;
}

QString AppConfig::dataDir() const
{
    QDir appDir(QCoreApplication::applicationDirPath());
    if (appDir.exists(QStringLiteral("data"))) {
        return appDir.absoluteFilePath(QStringLiteral("data"));
    }

    QDir cwd(QDir::current());
    if (cwd.exists(QStringLiteral("data"))) {
        return cwd.absoluteFilePath(QStringLiteral("data"));
    }

    appDir.mkpath(QStringLiteral("data/settings"));
    return appDir.absoluteFilePath(QStringLiteral("data"));
}

QString AppConfig::defaultSettingsPath() const
{
    return QDir(dataDir()).filePath("settings/settings.json");
}

bool AppConfig::loadJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_json = QJsonObject();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        m_json = QJsonObject();
        return false;
    }
    m_json = doc.object();
    return true;
}
