#pragma once

#include "AppConfig.h"
#include "SoundObject.h"
#include "UI/Interactive.h"
#include "UI/Button.h"

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
    void onClicked(ClickArgs& args);
    void enable();
    void enableInteractivity();
    void disable();
    void disableInteractivity();
    void endFade();

    bool selectScene;
    bool isPlaying;
    int activeSoundIdx;
    AppConfig* config;

    string scene_name;
    Button play_button;
    Button delete_scene;
    Button stop_button;
    vector<SoundObject*> sounds;
    TextInputField textfield;

    // Fading
    const float fadeDuration = 500;
    bool isFading;
    int fadeDirection;
    float fadeVolume;
    long int curTime;
    long int prevTime;
    std::function<void()> fadeCallback;

protected:
    bool bInteractive;
};

