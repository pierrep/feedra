#pragma once

#include <QJsonObject>
#include <QString>
#include <cstddef>

class AppConfig
{
public:
    AppConfig();

    void setup();

    // Where Feedra writes: settings, backups, the waveform cache. The data folder beside the
    // program when it can be written to (portable), otherwise the user's app data folder.
    QString dataDir() const;
    // What ships with the program and is only read (impulse responses): the data folder beside it.
    QString resourceDir() const;
    // The impulse response that ships with Feedra (data/ir/impulse.wav).
    QString defaultImpulsePath() const;
    QString defaultSettingsPath() const;
    // Settings that belong to this computer rather than to a project: the audio devices, the
    // window's size and place, MIDI. settings/local.json in the data folder, beside the
    // default project, so a project opened on another computer doesn't bring them along.
    QString localSettingsPath() const;
    QJsonObject loadLocalSettings() const;
    bool saveLocalSettings(const QJsonObject& settings) const;
    // data/cache/waveforms: saved waveform peaks, one file per sample (see PeakStore).
    QString waveformCacheDir() const;
    QString libraryLocation() const { return defaultLibraryLocation; }
    // Where a "load files" dialog should open: the folder of samplePath if it has one,
    // then the sample library from the settings, then the "files" folder next to the
    // loaded settings file.
    QString loadDialogDir(const QString& samplePath = QString()) const;

    float masterVolume() const { return m_masterVolume; }
    void setMasterVolume(float v) { m_masterVolume = v; }
    float masterFade() const { return m_masterFade; }
    void setMasterFade(float v) { m_masterFade = v; }

    bool loadJson(const QString& path);
    QJsonObject& json() { return m_json; }
    const QJsonObject& json() const { return m_json; }

    QString defaultLibraryLocation;
    QString lastPath;
    // The settings file the current scenes were loaded from, or last saved as.
    QString settingsPath;
    // Written into the app settings file on quit. When loadLastSettings is set, the next
    // launch opens lastSettingsPath instead of the default settings file.
    bool loadLastSettings = false;
    QString lastSettingsPath;

    int gridWidth = 6;
    int gridHeight = 4;
    unsigned int maxScenes = 14;
    // Save scenes and pads to the open settings file on quit. When off, only app settings
    // are written on quit, and scene changes are kept only by saving.
    bool autosave = true;
    // Go to scene (the scene row's Go button, Enter): how long the playing scenes take to fade
    // out while the new one fades in.
    double crossfadeSeconds = 3.0;
    // Talk-over (hold Space): how far every scene dips, in dB.
    double talkOverDepthDb = -12.0;
    // How long the dip takes going down (hold) and coming back up (release), in seconds.
    double talkOverFadeInSeconds = 0.15;
    double talkOverFadeOutSeconds = 0.7;
    // Panic (Esc): how long everything takes to fade out. A second press stops at once.
    double panicSeconds = 1.0;
    // The keyboard plays the pads (see KeyLayout).
    bool keyboardPads = true;
    // The output device by name, or empty to follow the system's default output.
    QString outputDevice;
    // How big the output buffer is (AudioLatency): "default", "low", "lowest" or "safe".
    // Applies when Feedra next starts.
    QString outputLatency = QStringLiteral("default");
    // A second device for the Files tab's previews (headphones), or empty to preview on the
    // main output. See CueOutput.
    QString previewDevice;

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
    mutable QString m_dataDir; // worked out once
};
