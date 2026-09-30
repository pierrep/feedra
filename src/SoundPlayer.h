#pragma once

#include "AudioSample.h"

#include <QElapsedTimer>
#include <QObject>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

class AppConfig;
class OpenALSoundPlayer;

class SoundPlayer : public QObject
{
    Q_OBJECT
public:
    explicit SoundPlayer(QObject* parent = nullptr);
    ~SoundPlayer() override;

    void setup(AppConfig* conf, int id);
    void close();
    void play();
    void stop();
    void playSample(int index);
    bool load(const std::filesystem::path& fileName, bool stream = false);
    bool load(const std::filesystem::path& fileName, int idx, bool stream = false);
    void unload();
    void update();
    void setPan(float pan);
    void setSpeed(float spd);
    void setPaused(bool bP);
    // Repeat (pad): after the last sample, start the list again after the random delay.
    void setRepeat(bool on);
    void setVolume(float vol);
    // Pad-level volume (pad x master x fades), before each sample's own gain. Stored so a
    // sample can be given its correct volume *before* it starts, not a UI tick later.
    void setBaseVolume(float vol);
    void setPosition(float pct);
    void setPositionMS(int ms);
    // Seek the current sample (or the delay countdown) the same way the Waveform tab does:
    // flushes queued stream audio so the playhead and what you hear jump together.
    void seekTo(float pct);
    // Seek one sample directly (the Waveform tab), withdrawing any scheduled handover first.
    void seekSample(int index, float pct);
    void setMinDelay(int delay);
    void setMaxDelay(int delay);

    float getPosition() const;
    int getPositionMS() const;
    bool isPlaying() const;
    bool isPlayingDelay() const;
    // True when playing would carry on from where it was paused, rather than start afresh.
    bool isResuming() const { return !player.empty() && !bStartFromBeginning; }
    bool isLoaded() const;
    bool isRepeating() const;
    // Loop (sample): the current sample loops seamlessly in the engine. The pad's loop icon
    // shows and switches this; it is the sample's saved setting.
    bool isCurrentSampleLooping() const;
    void setCurrentSampleLooping(bool on);
    float getSpeed() const;
    float getPan() const;
    float getVolume() const;
    float getDuration() const;
    int getSampleRate() const;
    int getNumChannels() const;
    int getCurSound() const;
    int getMinDelay() const;
    int getMaxDelay() const;
    float getTotalDelay() const;
    float getReverbSend() const;
    void setReverbSend(float send);
    float getReverbSend2() const;
    void setReverbSend2(float send);
    // Any of the four effect sends (see OpenALSoundPlayer::kSendCount).
    float getSend(int bus) const;
    void setSend(int bus, float send);
    void recalculateDelay(int id);
    // The delay can be switched off without losing min/max. While off, samples follow on
    // straight away (and hand over seamlessly).
    void setDelayEnabled(bool on);
    bool isDelayEnabled() const { return bDelayEnabled; }
    // Delay on and a non-zero min or max.
    bool hasDelay() const { return bDelayEnabled && (minDelay > 0 || maxDelay > 0); }
    // Seconds left of the delay countdown now running, or 0.
    float getRemainingDelay() const;

    void setRandomPlayback(bool val) { bRandomPlayback = val; }
    bool isPlayingRandom() const { return bRandomPlayback; }

    void setRandomPan(bool val) { bRandomPan = val; }
    bool isRandomPan() const { return bRandomPan; }

    AppConfig* config = nullptr;
    std::vector<AudioSample*> player;
    int minDelay = 0;
    int maxDelay = 0;
    bool bPlayingDelay = false;
    int curSound = 0;
    int id = 0;

    bool bPaused = true;
    bool bRepeat = false;
    bool bPlayBackEnded = false; // unused; kept for compatibility
    bool bCheckPlayBackEnded = false;
    bool bRandomPlayback = false;
    bool bRandomPan = false;
    bool bDelayEnabled = true;

signals:
    void panRandomised(int sampleIndex, float pan);

private:
    // Scheduled handover: the next sample is started by the audio device on the frame after
    // the current one's last, instead of on the next UI tick after it ends.
    struct Handover {
        bool armed = false;
        int next = -1;
        int from = -1;
        int64_t endNs = 0;
    };
    void tryArmHandover();
    void checkHandover();
    void cancelHandover();
    int chooseNext() const;
    void advanceAfterEnd();
    void applyRandomPan(int index);
    void applyRandomPanOnStart();
    void applyVolumeToCurrent();
    void onPlaybackEnded(OpenALSoundPlayer* ended);
    void handleEnded(OpenALSoundPlayer* ended);
    int indexOf(const OpenALSoundPlayer* audio) const;
    float randomRange(float minV, float maxV) const;
    float randomF() const;

    QElapsedTimer clock;
    qint64 prevMs = 0;
    bool bStartFromBeginning = true;
    Handover handover;
    // "Playback ended" notices, posted by stream threads and handled on the UI tick.
    std::mutex endedMutex;
    std::vector<OpenALSoundPlayer*> endedQueue;
    // Notices kept for a later tick: from the scheduled next sample before it is current,
    // or from the current sample while the pad is paused.
    std::vector<OpenALSoundPlayer*> heldEnded;
    float baseVolume = -1.0f; // unknown until the pad sets it
};
