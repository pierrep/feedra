#include "AppConfig.h"

#include <QCoreApplication>
#include <QDebug>
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
    autosave = true;
}

namespace {

// True when files can be created in `dir` (made if missing). A real write, because on Windows
// the permission bits don't tell you about Program Files.
bool canWriteIn(const QString& dir)
{
    if (!QDir().mkpath(dir)) {
        return false;
    }
    QFile probe(QDir(dir).filePath(QStringLiteral(".feedra-write-test")));
    if (!probe.open(QIODevice::WriteOnly)) {
        return false;
    }
    probe.close();
    probe.remove();
    return true;
}

} // namespace

QString AppConfig::resourceDir() const
{
    QDir appDir(QCoreApplication::applicationDirPath());
    if (appDir.exists(QStringLiteral("data"))) {
        return appDir.absoluteFilePath(QStringLiteral("data"));
    }
    QDir cwd(QDir::current());
    if (cwd.exists(QStringLiteral("data"))) {
        return cwd.absoluteFilePath(QStringLiteral("data"));
    }
    return appDir.absoluteFilePath(QStringLiteral("data"));
}

QString AppConfig::dataDir() const
{
    if (!m_dataDir.isEmpty()) {
        return m_dataDir;
    }
    // Portable, as before: settings, backups and the waveform cache live in the data folder
    // beside the program, when that folder can be written to.
    const QString beside = resourceDir();
    if (canWriteIn(QDir(beside).filePath(QStringLiteral("settings")))) {
        m_dataDir = beside;
        return m_dataDir;
    }
    // Installed (Program Files, /usr, an app bundle): the user's own app data folder instead.
    QString user = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (user.isEmpty()) {
        user = QDir::home().filePath(QStringLiteral(".feedra"));
    }
    QDir().mkpath(QDir(user).filePath(QStringLiteral("settings")));
    // First run: start from the settings that came with the program, if any.
    const QString bundled = QDir(beside).filePath(QStringLiteral("settings/settings.json"));
    const QString mine = QDir(user).filePath(QStringLiteral("settings/settings.json"));
    if (QFile::exists(bundled) && !QFile::exists(mine)) {
        QFile::copy(bundled, mine);
        QFile(mine).setPermissions(QFile(mine).permissions() | QFileDevice::WriteOwner | QFileDevice::WriteUser);
    }
    qInfo() << "Data folder beside the program is read-only; using" << user;
    m_dataDir = user;
    return m_dataDir;
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

QString AppConfig::defaultImpulsePath() const
{
    return QDir(resourceDir()).filePath(QStringLiteral("ir/impulse.wav"));
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
