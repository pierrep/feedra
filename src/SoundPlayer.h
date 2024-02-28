#pragma once

#include "Interactive.h"
#include "AppConfig.h"
#include "OpenALSoundPlayer.h"

class SoundPlayer: public Interactive
{
public:
    SoundPlayer();
    ~SoundPlayer();
    SoundPlayer(const SoundPlayer& d);
    void setup(AppConfig* conf, int id);
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
    float getPosition() const;
    int getPositionMS() const;
    bool isPlaying() const;
    bool isPlayingDelay() const;
    bool isLoaded() const;
    float getSpeed() const;
    float getPan() const;
    float getVolume() const;
    int getSampleRate() const;
    int getNumChannels() const ;
    void recalculateDelay();

    AppConfig* config;
    vector<OpenALSoundPlayer*> audioPlayer;
    vector<int> minDelay;
    vector<int> maxDelay;
    vector<int> totalDelay;
    vector<int> curDelay;
    bool bPlayingDelay;
    int curSound;

    unsigned long curTime;
    unsigned long prevTime;

    bool bPaused;
    bool bIsLooping;

    int id;
};