#ifndef AUDIOSAMPLE_H
#define AUDIOSAMPLE_H

#include "OpenALSoundPlayer.h"
#include "ofVec3f.h"
#include "AppConfig.h"

class AudioSample
{
public:
    AudioSample();
    AudioSample(const AudioSample& parent);
    void render(ofVec3f pos);

    OpenALSoundPlayer* audioPlayer;
    int totalDelay;
    int curDelay;
    float gain; //0.5f - 2.0f ?
    float pitch;
    std::string sample_path;
    AppConfig* config;
};

#endif // AUDIOSAMPLE_H
