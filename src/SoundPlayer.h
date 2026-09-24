#pragma once

#include "AudioSample.h"

#include <QElapsedTimer>
#include <QObject>
#include <filesystem>
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
    bool load(const std::filesystem::path& fileName, bool stream = false);
    bool load(const std::filesystem::path& fileName, int idx, bool stream = false);
    void unload();
    void update();
    void setPan(float pan);
    void setSpeed(float spd);
    void setPaused(bool bP);
    void setLoop(bool bLp);
    void setVolume(float vol);
    void setPosition(float pct);
    void setPositionMS(int ms);
    void setMinDelay(int delay);
    void setMaxDelay(int delay);

    float getPosition() const;
    int getPositionMS() const;
    bool isPlaying() const;
    bool isPlayingDelay() const;
    bool isLoaded() const;
    bool isLooping() const;
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
    void recalculateDelay(int id);

    void setRandomPlayback(bool val) { bRandomPlayback = val; }
    bool isPlayingRandom() const { return bRandomPlayback; }

    void setRandomPan(bool val) { bRandomPan = val; }
    bool isRandomPan() const { return bRandomPan; }
    bool isSpatialisedStereo(int index) const;
    void setSpatialisedStereo(int index, bool val);

    AppConfig* config = nullptr;
    std::vector<AudioSample*> player;
    int minDelay = 0;
    int maxDelay = 0;
    bool bPlayingDelay = false;
    int curSound = 0;
    int id = 0;

    bool bPaused = true;
    bool bIsLooping = false;
    bool bPlayBackEnded = false;
    bool bCheckPlayBackEnded = false;
    bool bRandomPlayback = false;
    bool bRandomPan = false;

private:
    void onPlaybackEnded(OpenALSoundPlayer* ended);
    float randomRange(float minV, float maxV) const;
    float randomF() const;

    QElapsedTimer clock;
    qint64 prevMs = 0;
};
