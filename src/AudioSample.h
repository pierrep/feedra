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
    void setup();
    void render(ofVec3f pos);    
    void onClicked(ClickArgs& args);
    void enableEditorMode();
    void disableEditorMode();
    void setPitch(float val);
    void setGain(float val);
    void setPan(float val) {audioPlayer->setPan(val);}
    float getPitch(){ return pitch;}
    float getGain(){ return gain;}
    float getPan(){ return audioPlayer->getPan();}
    bool isSpatialisedStereo() {return audioPlayer->isSpatialisedStereo();}

    OpenALSoundPlayer* audioPlayer;
    std::string sample_path;
    AppConfig* config;

    bool bEditorMode;
    bool bSelected;
    static int count; // used to auto-generate a unique id
    static ofEvent<int> clickedSampleEvent;

    float totalDelay; // in secs
    float curDelay; // in secs

protected:
    float gain; //0.5f - 2.0f ?
    float pitch;
};

#endif // AUDIOSAMPLE_H
