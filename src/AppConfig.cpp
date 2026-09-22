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
    auto findBinData = [](QDir dir, int maxUp) -> QString {
        for (int i = 0; i < maxUp; ++i) {
            if (dir.exists(QStringLiteral("bin/data"))) {
                return dir.absoluteFilePath(QStringLiteral("bin/data"));
            }
            if (!dir.cdUp()) {
                break;
            }
        }
        return {};
    };

    // Prefer the project bin/data folder (same path the original OF app used).
    // CMake copies data next to the exe, so applicationDir/data would otherwise
    // win and Save would not update bin/data/settings/settings.json.
    const QString fromApp = findBinData(QDir(QCoreApplication::applicationDirPath()), 8);
    if (!fromApp.isEmpty()) {
        return fromApp;
    }
    const QString fromCwd = findBinData(QDir::current(), 8);
    if (!fromCwd.isEmpty()) {
        return fromCwd;
    }

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
