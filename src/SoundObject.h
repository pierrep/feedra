#pragma once

#include "UI/TextInputField.h"
#include "UI/SimpleSlider.h"
#include "AppConfig.h"
#include "UI/Interactive.h"
#include "UI/LoadButton.h"
#include "UI/PlayButton.h"
#include "UI/PlayBar.h"
#include "UI/StopButton.h"
#include "UI/Looper.h"
#include "SoundPlayer.h"

//#define MAX_SOUNDS_PER_OBJECT 24

class SoundObject: public Interactive
{
public:

    SoundObject();
    ~SoundObject();
    SoundObject(const SoundObject& d);
    SoundObject(AppConfig* config, size_t _scene_id, int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render();
    void update();
    void onClicked(ClickArgs& args);
    void save();
    void load();
    void load(string newpath);
    void play() {playButton.doPlay = true;}
    void stop() {stopper.doStop = true;}
    void disableAllEvents();
    void enableAllEvents();
    void onDragEvent(ofDragInfo &args);
    void setupSound(string path);
    bool loadSingleSound(std::string filepath, bool bClearSounds);
    void loadMultipleSounds(vector<string>& filePaths,bool bClearSounds);
    void enableEditorMode();
    void disableEditorMode();

    //Globals    
    AppConfig* config;
    SoundPlayer soundPlayer;

    LoadButton loader;
    PlayButton playButton;
    PlayBar playbar;
    StopButton stopper;
    Looper looper;

    size_t scene_id;

    bool isSetup;
    bool isLooping;
    bool isStream;
    bool isPaused;

    SimpleSlider volumeslider;
    TextInputField   soundname;
    vector<string> soundpath;
    string libraryLocation;

    int sample_rate;
    int channels;

    float reverbSend;
    float fadeVolume;

    static ofEvent<size_t> clickedObjectEvent;
};
