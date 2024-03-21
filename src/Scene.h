#pragma once

#include "AppConfig.h"
#include "SoundObject.h"
#include "UI/Interactive.h"
#include "UI/PlayScene.h"
#include "UI/DeleteScene.h"
#include "UI/Stop.h"

class Scene: public Interactive
{
public:
    Scene();
    ~Scene();
    Scene(const Scene& d);
    Scene(AppConfig* config, string name,int _id, int _activeSound, int _x, int _y, int _w, int _h);
    void setup();
    void setup(string newpath);
    void render();
    void update();
    void play();
    void pause();
    void stop();
    void updatePosition(int x,int y);
    void onClicked(int& args);
    void enable();
    void disable();

    int id;
    bool selectScene;
    bool isPlaying;
    int activeSound;
    AppConfig* config;

    string scene_name;
    PlayScene play_button;    
    DeleteScene delete_scene;
    Stop stop_button;
    vector<SoundObject*> sounds;
    TextInputField textfield;

    // Fading
    const float fadeDuration = 1000;
    bool isFading;
    int fadeDirection;
    float fadeVolume;
    long int curTime;
    long int prevTime;
    std::function<void()> fadeCallback;
};

