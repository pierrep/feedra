#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QStandardPaths>

AppConfig::AppConfig() = default;

void AppConfig::setup()
{
    // Empty until the user picks one, so the load dialogs can fall back to the settings folder.
    defaultLibraryLocation.clear();
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

QString AppConfig::loadDialogDir(const QString& samplePath) const
{
    const QString settingsFile = settingsPath.isEmpty() ? defaultSettingsPath() : settingsPath;
    const QDir settingsDir = QFileInfo(settingsFile).absoluteDir();

    if (!samplePath.isEmpty()) {
        // Relative sample paths are stored relative to the settings file.
        const QFileInfo sample(QDir::isRelativePath(samplePath) ? settingsDir.filePath(samplePath) : samplePath);
        if (sample.absoluteDir().exists()) {
            return sample.absolutePath();
        }
    }
    if (!defaultLibraryLocation.isEmpty() && QDir(defaultLibraryLocation).exists()) {
        return defaultLibraryLocation;
    }
    const QString files = settingsDir.filePath(QStringLiteral("files"));
    if (QDir(files).exists()) {
        return files;
    }
    if (settingsDir.exists()) {
        return settingsDir.absolutePath();
    }
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return music.isEmpty() ? QDir::homePath() : music;
}

QString AppConfig::waveformCacheDir() const
{
    return QDir(dataDir()).filePath(QStringLiteral("cache/waveforms"));
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
