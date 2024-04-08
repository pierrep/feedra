#ifndef AUDIOSAMPLE_H
#define AUDIOSAMPLE_H

#include "OpenALSoundPlayer.h"
#include "ofVec3f.h"
#include "AppConfig.h"
#include "Interactive.h"

class AudioSample: public Interactive
{
public:
    ~AudioSample();
    AudioSample();
    AudioSample(const AudioSample& parent);
    void render(ofVec3f pos);
    void onClicked(ClickArgs& args);
    void enableEditorMode();
    void disableEditorMode();

    OpenALSoundPlayer* audioPlayer;
    int totalDelay;
    int curDelay;
    float gain; //0.5f - 2.0f ?
    float pitch;
    std::string sample_path;
    AppConfig* config;

    static ofEvent<size_t> clickedObjectEvent;

    bool bEditorMode;
    bool bSelected;
};

#endif // AUDIOSAMPLE_H
