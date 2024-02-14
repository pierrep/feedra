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

    ofTrueTypeFont& f1() {return mainfont;}
    ofTrueTypeFont& f2() {return smallfont;}
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

    bool loopByDefault;

    int activeScene;
    size_t activeSceneIdx;
    int max_scenes;
    int activeSound;
    size_t activeSoundIdx;
    string last_path;

    ofImage loopicon;

    ofJson settings;
};
