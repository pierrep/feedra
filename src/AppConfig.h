#pragma once

#include <QJsonObject>
#include <QString>
#include <cstddef>

class AppConfig
{
public:
    AppConfig();

    void setup();

    QString dataDir() const;
    QString defaultSettingsPath() const;
    QString libraryLocation() const { return defaultLibraryLocation; }

    float masterVolume() const { return m_masterVolume; }
    void setMasterVolume(float v) { m_masterVolume = v; }
    float masterFade() const { return m_masterFade; }
    void setMasterFade(float v) { m_masterFade = v; }

    bool loadJson(const QString& path);
    QJsonObject& json() { return m_json; }
    const QJsonObject& json() const { return m_json; }

    QString defaultLibraryLocation;
    QString lastPath;

    int gridWidth = 6;
    int gridHeight = 4;
    unsigned int maxScenes = 14;
    bool loopByDefault = false;

    int activeSceneId = 0;
    int activeSceneIdx = 0;
    int prevSceneIdx = 0;
    int activeSoundIdx = 0;
    int prevSoundIdx = 0;
    int activeSampleIdx = 0;
    int activeSampleId = 0;

    bool dragging = false;

private:
    float m_masterVolume = 1.0f;
    float m_masterFade = 1.0f;
    QJsonObject m_json;
};
