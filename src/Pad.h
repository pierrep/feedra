#pragma once

// One pad of a scene: its samples, its settings and its player. No widgets: the pad grid shows
// it with a SoundPadWidget, the window's controls edit it, and the tests use it on its own.
//
// Samples load on the loader threads (SampleLoadQueue). Results come back through
// submitDecoded() in the order they were asked for. While loading, the pad keeps every
// sample's settings so saving, copying and moving never lose one.

#include "OpenALSoundPlayer.h"
#include "SoundPlayer.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <map>
#include <memory>
#include <vector>

class AppConfig;
class SampleLoadQueue;

class Pad : public QObject
{
    Q_OBJECT
public:
    Pad(AppConfig* config, int sceneId, int padId, QObject* parent = nullptr);
    ~Pad() override;

    int padId() const { return m_padId; }
    int sceneId() const { return m_sceneId; }
    SoundPlayer& soundPlayer() { return m_player; }
    const SoundPlayer& soundPlayer() const { return m_player; }

    QString soundName() const { return m_name; }
    void setSoundName(const QString& name);
    // Linear 0..1, in the slider's 0.1 dB steps (so what's saved is what's shown).
    float padVolume() const { return m_volume; }
    void setPadVolume(float value);
    // Repeat (pad): start the list again after the last sample, after the random delay.
    bool isRepeating() const;
    void setRepeating(bool repeat);

    bool isLoaded() const;
    bool isLoading() const;
    bool isPlaying() const;
    // True when the pad has samples: loaded, still loading, or missing from disk.
    bool hasSounds() const;
    // True when min or max delay is above zero, so the pad waits before it plays.
    bool hasDelay() const;
    int loadTotal() const { return m_loadTotal; }
    int loadDone() const { return m_nextCommit; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }

    // Every sample path on the pad, in order, and the one it would play next.
    QStringList samplePaths() const;
    QString samplePath(int index) const;
    QString currentSamplePath() const;
    // Samples from the project file that couldn't be loaded (file missing or unreadable).
    // They stay in the project when it's saved.
    QStringList missingPaths() const;

    // The pad's play button: play, or pause if it's playing. Nothing while it loads or is
    // empty. The keyboard and MIDI use this too.
    void togglePlay();
    // Play, pause and stop with the pad's own fade in and fade out times (instant at 0).
    // Pressing play during a fade-out fades back in from where it was.
    void play();
    void pause();
    void stop();
    // Straight away, whatever the fade times: the scene has already faded the pad out.
    void pauseNow();
    void stopNow();

    // Seconds (0 = none, up to 10).
    float fadeInTime() const { return m_fadeIn; }
    void setFadeInTime(float seconds);
    float fadeOutTime() const { return m_fadeOut; }
    void setFadeOutTime(float seconds);
    // The pad's own fade level, 0..1 (1 when not fading).
    float padFadeGain() const;
    bool isPadFading() const { return m_padFadeDirection != 0; }

    // Once per UI tick: the player's tick and the pad's volume.
    void updateAudio();
    // Pad volume x master x fades x intensity layer, applied straight away.
    void setFadeVolume(float fade);
    float fadeVolume() const { return m_fadeVolume; }
    void applyVolume();

    void setLoadQueue(SampleLoadQueue* queue) { m_queue = queue; }
    void submitDecoded(int index, int generation, DecodedAudio audio);

    // Relative sample paths in the file are resolved against `baseDir` (the project file's folder).
    void loadFromJson(const QJsonObject& root, const QString& baseDir = QString());
    void saveToJson(QJsonObject& sceneObj) const;
    void loadFiles(const QStringList& paths, bool clearExisting);
    void clearPad();

    // Effect send `bus`: 0 and 1 are the EAX reverbs, 2 and 3 the convolution reverbs.
    void setSend(int bus, float send);
    float sendLevel(int bus) const;

    // Settings and samples copied from a pad. Independent of the source, so a cut can clear
    // that pad and a paste can land on a pad in another scene.
    struct PadClip {
        bool valid = false;
        bool stream = true;
        int sampleRate = 0;
        int channels = 0;
        bool repeat = false;
        bool delayOn = true;
        int minDelay = 0;
        int maxDelay = 0;
        bool randomPlayback = false;
        bool randomPan = false;
        QString name;
        float volume = 0.7f;
        float sends[OpenALSoundPlayer::kSendCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
        struct Sample {
            QString path;
            float pitch = 1.0f;
            float gain = 1.0f;
            float pan = 0.0f;
            bool panRandom = false;
            float width = 1.0f;
            LoopRegion loop;
            bool operator==(const Sample& o) const
            {
                return path == o.path && pitch == o.pitch && gain == o.gain && pan == o.pan
                    && panRandom == o.panRandom && width == o.width && loop == o.loop;
            }
        };
        std::vector<Sample> samples;
        // Samples whose files couldn't be loaded, as they were saved. Kept so they aren't lost.
        std::vector<QJsonObject> missing;
        bool scenePad = false;
        float fadeIn = 0.0f;
        float fadeOut = 0.0f;
        int layer = 0; // Intensity::Layer
        bool operator==(const PadClip& o) const;
        bool operator!=(const PadClip& o) const { return !(*this == o); }
        // The same files in the same order (settings aside).
        bool sameSamples(const PadClip& o) const;
    };
    PadClip clip() const;
    void pasteClip(const PadClip& clip);
    // Adds the clip's samples after this pad's own, each with its own editor settings
    // (pitch, gain, pan, width, loop). Nothing else about this pad changes.
    void appendClip(const PadClip& clip);
    void copyFrom(const Pad& other) { pasteClip(other.clip()); }
    // Makes the pad what `clip` describes (undo and redo). With the same files, only the
    // settings change and nothing reloads; otherwise it's a paste (an invalid clip empties it).
    void restoreClip(const PadClip& clip);

    // A scene pad starts when its scene is played from the scene list. If a scene has any,
    // only they start; otherwise every pad with sounds does.
    bool isScenePad() const { return m_scenePad; }
    void setScenePad(bool on);

    // The pad's intensity layer (Intensity::Layer: None, Calm, Tense or Combat). Layered pads
    // start with their scene, and the scene's Intensity slider sets how much of each is heard.
    int layer() const { return m_layer; }
    void setLayer(int layer);
    // Set by the scene every tick from its Intensity slider (1 without a layer).
    float layerGain() const { return m_layerGain; }
    void setLayerGain(float gain);

    void removeSampleAt(int index);
    // Returns where it went, or -1.
    int moveSample(int from, int insertIndex);
    // The pad's loop icon: the current sample's loop.
    void setCurrentSampleLooping(bool on);

    // Settings were changed from outside (the delay on the Pad tab): redraw.
    void notifyChanged() { emit changed(); }

    // Asks before a drop brings in a lot of files at once. True to go on. (Needs a parent widget.)
    static bool confirmFileCount(class QWidget* parent, int count);

signals:
    void loadStateChanged();
    void loadingFinished();
    // Files added by loadFiles() or a paste have all loaded.
    void filesDropped();
    void scenePadChanged();
    void sampleLoopChanged();
    void nameChanged(const QString& name);
    void volumeChanged(float volume);
    // Anything that changes how the pad looks (samples, delay, cleared).
    void changed();

private:
    struct LoadSlot {
        QString path;
        float pitch = 1.0f;
        float gain = 1.0f;
        float pan = 0.0f;
        bool panRandom = false;
        float width = 1.0f;
        LoopRegion loop;
        bool failed = false;
        QJsonObject saved; // the sample as the project file had it, if it came from one
    };

    // Load generations come from one app-wide counter, so a job queued for a pad that has
    // since been deleted can never match a new pad that reuses its scene and pad ids.
    static int newLoadGeneration();
    void cancelLoading();
    void enqueueSample(const LoadSlot& slot);
    bool commitDecoded(int slotIndex, DecodedAudio audio);
    void flushIncoming();
    std::vector<LoadSlot> sampleSpecs() const;

    AppConfig* m_config = nullptr;
    SampleLoadQueue* m_queue = nullptr;
    int m_sceneId = 0;
    int m_padId = 0;
    SoundPlayer m_player;
    QString m_name;
    float m_volume = 0.0f;
    float m_fadeVolume = 1.0f;
    bool m_stream = true;
    int m_sampleRate = 0;
    int m_channels = 0;
    bool m_scenePad = false;
    int m_layer = 0;
    float m_layerGain = 1.0f;

    // The pad's own fade (play, pause and stop from the pad, keys and MIDI).
    enum class AfterFade { None, Pause, Stop };
    float m_fadeIn = 0.0f;
    float m_fadeOut = 0.0f;
    int m_padFadeDirection = 0; // 1 in, -1 out, 0 not fading
    float m_padPhase = 1.0f;
    float m_padPhaseFrom = 1.0f;
    int m_padFadeMs = 0;
    AfterFade m_afterFade = AfterFade::None;
    QElapsedTimer m_padFadeClock;
    void startPadFade(int direction, float seconds, AfterFade after);
    void updatePadFade();
    // Send levels, kept here too so they survive loading (bus order: EAX 1, EAX 2, conv 1, conv 2).
    float m_sends[OpenALSoundPlayer::kSendCount] = { 0.0f, 0.0f, 0.0f, 0.0f };

    std::shared_ptr<std::atomic<int>> m_loadGeneration = std::make_shared<std::atomic<int>>(newLoadGeneration());
    std::vector<LoadSlot> m_slots;
    std::vector<QJsonObject> m_missingSamples;
    std::map<int, DecodedAudio> m_incoming;
    int m_loadTotal = 0;
    int m_nextCommit = 0;
    bool m_finishSent = false;
    bool m_notifyWhenDone = false;
};
