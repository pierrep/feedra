#pragma once

#include "OpenALSoundPlayer.h"

#include <string>

class AudioSample
{
public:
    AudioSample();
    ~AudioSample();

    void setPitch(float val);
    void setGain(float val);
    void setPan(float val);
    float getPitch() const { return pitch; }
    float getGain() const { return gain; }
    float getPan() const;
    bool isSpatialisedStereo() const;

    OpenALSoundPlayer* audioPlayer = nullptr;
    std::string sample_path;
    int id = 0;

    float totalDelay = 0.0f;
    float curDelay = 0.0f;

private:
    float gain = 1.0f;
    float pitch = 1.0f;
};
