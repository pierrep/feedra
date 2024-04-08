#pragma once

#include "UI/Interactive.h"
#include "AppConfig.h"
#include "AudioSample.h"

class SoundPlayer: public Interactive
{
public:
    SoundPlayer();
    ~SoundPlayer();
    SoundPlayer(const SoundPlayer& d);
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
    float getGain() const;
    float getSpeed() const;
    float getPan() const;
    float getVolume() const;
    float getDuration() const;
    int getSampleRate() const;
    int getNumChannels() const;
    int getCurSound() const;
    bool getIsStereo() const;
    int getMinDelay() const;
    int getMaxDelay() const;
    int getTotalDelay() const;
    float getReverbSend() const;
    void setReverbSend(float send);
    void recalculateDelay(int id);
    void playbackEnded(OpenALSoundPlayer* &args);

    AppConfig* config;
    vector<AudioSample> player;
    int minDelay;
    int maxDelay;
    bool bPlayingDelay;
    int curSound;

    unsigned long curTime;
    unsigned long prevTime;

    bool bPaused;
    bool bIsLooping;
    bool bPlayBackEnded;
    bool bCheckPlayBackEnded;

    string filename;
    //int id;

    ofEvent<OpenALSoundPlayer*> playbackEndedEvent;
};
