#include "AppConfig.h"

AppConfig::AppConfig()
{

}

//--------------------------------------------------------------
AppConfig::~AppConfig()
{
    ofLogVerbose() << "AppConfig destructor called...";
}

//--------------------------------------------------------------
void AppConfig::setup()
{
    xoffset = 20*x_scale;
    yoffset = 80*y_scale;
    size = 120*x_scale;
    spacing = size + 30*x_scale;
    gridWidth = 6;
    gridHeight = 4;

    scene_width = 240*x_scale;
    scene_height = 45*y_scale;
    scene_spacing = scene_height + 5*x_scale;
    baseSceneOffset = gridWidth*spacing ;
    scene_yoffset = 80*y_scale;
    max_scenes = 14;

    activeScene = 0;
    activeSoundIdx = 0;

    loopByDefault = false;

    smallfont.load("fonts/NewMediaFett.ttf", 9 * x_scale, true, false);
    mainfont.load("fonts/NewMediaFett.ttf", 12 * x_scale, true, false);
    tinyfont.load("fonts/NewMediaFett.ttf", 7 * x_scale, true, false);

    loopicon.load("images/loopicon.png");

    defaultLibraryLocation = "/home/grimus/Downloads/5e/";

    settings.clear();

    masterVolume = 1.0f;
    masterFade = 1.0f;
}


