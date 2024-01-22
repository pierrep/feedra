#pragma once

#include "TextInputField.h"
#include "SimpleSlider.h"
#include "AppConfig.h"
#include "Interactive.h"
#include "Loader.h"
#include "Player.h"
#include "PlayBar.h"
#include "Stop.h"
#include "Looper.h"

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
    void onClicked(int& args);
    void textFieldEnter(string& newText);
    void save();
    void load();
    void play() {player.doPlay = true;}
    void disableAllEvents();
    void enableAllEvents();

    int id;

    //Globals    
    AppConfig* config;
    ofSoundPlayer audioPlayer;
    ofJson settings;

    Loader loader;
    Player player;
    PlayBar playbar;
    Stop stopper;
    Looper looper;

    size_t scene_id;

    bool isSetup;
    bool isLooping;
    bool isStream;
    bool isPaused;

    SimpleSlider volumeslider;
    TextInputField   soundname;
    string soundpath;
    string libraryLocation;

};
