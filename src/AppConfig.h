#pragma once

#include "ofMain.h"

class AppConfig
{
public:
    AppConfig();
    ~AppConfig();
    void setup();

    // fonts
    ofTrueTypeFont  mainfont;
    ofTrueTypeFont  smallfont;
    ofTrueTypeFont  tinyfont;

    ofTrueTypeFont& f1() {return mainfont;}
    ofTrueTypeFont& f2() {return smallfont;}
    ofTrueTypeFont& f3() {return tinyfont;}

    const string getLibraryLocation() { return defaultLibraryLocation; }
    const float getMasterVolume() { return masterVolume;}

    string defaultLibraryLocation;

    float masterVolume;
    float masterFade;
    float x_scale;
    float y_scale;
    int xoffset;
    int yoffset;
    int size;
    int spacing;
    int gridWidth;
    int gridHeight;
    int scene_spacing;
    int scene_width;
    int scene_height;
    int baseSceneOffset;
    int scene_yoffset;
    int sample_gui_width;
    bool loopByDefault;
    int activeScene;
    unsigned int max_scenes;
    size_t activeSceneIdx;
    size_t prevSceneIdx;
    size_t activeSoundIdx;
    size_t activeSampleIdx;
    //size_t activeSample;
    string last_path;

    ofImage loopicon;
    ofJson settings;
};
