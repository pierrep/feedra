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

    float x_scale;
    float y_scale;

    int xoffset;
    int yoffset;
    int size;
    int spacing;
    int gridWidth;
    int gridHeight;

    bool loopByDefault;

    size_t activeScene;
    size_t activeSound;
    string last_path;

    ofImage loopicon;
};
