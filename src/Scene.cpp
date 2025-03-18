#include "Scene.h"
#include "AL/alc.h"

Scene::Scene()
{   
    bInteractive = false;
    //bLoading = false;
}

//--------------------------------------------------------------
Scene::~Scene()
{
    for(size_t i=0;i < sounds.size();i++) {
        delete sounds[i];
    }
    sounds.clear();

    ofRemoveListener(Interactive::clickedEvent, this, &Scene::onClicked);
    //ofLogNotice() << "Scene destructor for id: " << id << " called...";
}

//--------------------------------------------------------------
void Scene::setup() {
    setup("settings/settings.json");
}

//--------------------------------------------------------------
void Scene::setup(string _newpath)
{    
    //bLoading = true;
    ofAddListener(this->clickedEvent, this, &Scene::onClicked);
    enableInteractivity();

    textfield.setUseListeners(true);
    //newpath = _newpath;

    bool bDoThreading = alcIsExtensionPresent(OpenALSoundPlayer::getCurrentDevice(), "ALC_EXT_thread_local_context");
    bDoThreading = false;

    if(bDoThreading)
    {
        //   main_context = alcGetCurrentContext();
        //startThread();
    } else {
        for(size_t i=0;i < config->gridWidth*config->gridHeight;i++) {
            int x = i%config->gridWidth*config->spacing + config->xoffset;
            int y = (i/config->gridWidth)*config->spacing + config->yoffset;
            SoundObject* s = new SoundObject(config,id,i,x,y,config->size,config->size);
            sounds.push_back(s);
        }

        // setup sound objects
        for(size_t i=0;i < sounds.size();i++) {
            //sounds[i]->setup();
            sounds[i]->loadThreaded();
        }
        //bLoading = false;
    }
}

//--------------------------------------------------------------
//void Scene::threadedFunction()
//{
//    if(alcIsExtensionPresent(OpenALSoundPlayer::getCurrentDevice(), "ALC_EXT_thread_local_context"))
//    {
//        ALCboolean (ALC_APIENTRY*alcSetThreadContext)(ALCcontext* context);
//        alcSetThreadContext = reinterpret_cast<ALCboolean (ALC_APIENTRY*)(ALCcontext *context)>(alcGetProcAddress(OpenALSoundPlayer::getCurrentDevice(), "alcSetThreadContext"));

//        alcSetThreadContext(main_context);

//        for(size_t i=0;i < config->gridWidth*config->gridHeight;i++) {
//            int x = i%config->gridWidth*config->spacing + config->xoffset;
//            int y = (i/config->gridWidth)*config->spacing + config->yoffset;
//            SoundObject* s = new SoundObject(config,id,i,x,y,config->size,config->size);
//            sounds.push_back(s);
//        }

//        // setup sound objects
//        for(size_t i=0;i < sounds.size();i++) {
//            sounds[i]->setup();
//            sounds[i]->load(newpath);
//        }

//        alcSetThreadContext(NULL);
//    }
//    bLoading = false;

//}

//--------------------------------------------------------------
Scene::Scene(AppConfig* _config, string name, int _id, int _activeSoundIdx, int _x, int _y, int _w, int _h)
{
    id = _id;

    config = _config;
    activeSoundIdx = _activeSoundIdx;
    bInteractive = false;

    setWidth(_w);
    setHeight(_h);

    play_button.setup(config,id,0,0,20 * config->x_scale, 20 * config->y_scale,ButtonType::PLAY_SCENE);
    delete_scene.setup(config,id,0,0,20 * config->x_scale, 20 * config->y_scale,ButtonType::DELETE_SCENE);
    stop_button.setup(config, id, 0, 0, 20 * config->x_scale, 20 * config->y_scale,ButtonType::STOP_SCENE);

    scene_name = "Scene "+ofToString(id+1);
    if(strcmp(name.c_str(),"")== 0) {
        textfield.text = scene_name;
    } else {
        textfield.text = name;
    }

    textfield.setFont(config->f2());
    textfield.disable();

    updatePosition(_x,_y);

    selectScene = false;
    isPlaying = false;
    isFading = false;

    fadeVolume = 1.0f;
}

//--------------------------------------------------------------
Scene::Scene(const Scene& parent) {
    ofLogNotice() << "Scene copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
    bInteractive = parent.bInteractive;
}

//---------------------------------------------------------
void Scene::updatePosition(int _x, int _y)
{
    setX(_x);
    setY(_y);

    play_button.setX(_x + getWidth()- 120 * config->x_scale);
    play_button.setY(_y + 10 * config->y_scale);

    stop_button.setX(_x + getWidth()- 90 * config->x_scale);
    stop_button.setY(_y + 10 * config->y_scale);

    delete_scene.setX(_x + getWidth()- 40 * config->x_scale);
    delete_scene.setY(_y + 10 * config->y_scale);

    float txt_height = 16;
    textfield.bounds = ofRectangle(getX()+10*config->x_scale, getY() + getHeight()/2 - txt_height/3*config->y_scale, 100*config->x_scale, txt_height*config->y_scale);
}

//---------------------------------------------------------
void Scene::onClicked(ClickArgs& args) {
    if(!bInteractive) return;

    ofLogNotice() << "Scene id: " << id << " clicked";
    if(config->activeScene != id) {
        config->activeScene = id;
        selectScene = true;
    }
}

//--------------------------------------------------------------
void Scene::render()
{
 //if(!bLoading)
 {
    if(config->activeScene == id) {
        for(size_t i=0; i < sounds.size();i++) {
             sounds[i]->render();
        }
    }
 }

    ofPushStyle();
    if(config->activeScene == id) {
        ofSetColor(255.0f,100,100);
    } else {
        ofSetColor(64);
    }
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());

    ofNoFill();
    if(config->activeScene == id) {
        ofSetColor(0);
    } else {
        ofSetColor(64);
    }
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());

    bool filePlaying = false;
    for(size_t i=0; i < sounds.size();i++) {
         if(sounds[i]->playButton.isPlaying)
         {
             filePlaying = true;
         }
    }
    if(config->activeScene == id || filePlaying) {
        play_button.draw();
    }    

    if(config->activeScene == id) {
        delete_scene.draw();
        stop_button.draw();
    }

    if(config->activeScene == id) {
        ofSetColor(0);
    } else {
        ofSetColor(128);
    }
    //config->smallfont.drawString(scene_name,getX()+10*config->x_scale, getY() + getHeight()/2);
    textfield.draw();
    if(textfield.isEditing()) {
        ofDrawRectangle(textfield.bounds);
    }
    ofPopStyle();   
}

//--------------------------------------------------------------
void Scene::play()
{
    isFading = true;
    prevTime = ofGetElapsedTimeMillis();
    fadeDirection = 0;
    fadeCallback = [this]() -> void
    {
        //std::cout << "End play fade-in" << endl;
    };

    for(size_t i=0; i < sounds.size();i++) {
        sounds[i]->soundPlayer.setPaused(false);
        sounds[i]->stopper.isStopped  = false;
    }
}

//--------------------------------------------------------------
void Scene::pause()
{
    isFading = true;
    prevTime = ofGetElapsedTimeMillis();
    fadeDirection = 1;
    fadeCallback = [this]() -> void
    {
        //std::cout << "End pause fade-out" << endl;
        for(size_t i=0; i < sounds.size();i++) {
            sounds[i]->soundPlayer.setPaused(true);
        }
    };

}

//--------------------------------------------------------------
void Scene::stop()
{
    isFading = true;
    prevTime = ofGetElapsedTimeMillis();
    fadeDirection = 1;
    fadeCallback = [this]() -> void
    {
        //std::cout << "End stop fade-out" << endl;
        for(size_t i=0; i < sounds.size();i++) {
            sounds[i]->soundPlayer.stop();
        }
    };

}

//--------------------------------------------------------------
void Scene::update()
{
    activeSoundIdx = config->activeSoundIdx;

    if(play_button.bActivate) {
        if(isPlaying) {
            isPlaying = false;
            pause();
        } else {
            isPlaying = true;
            play();
        }
        play_button.bActivate = false;
    }

    if(stop_button.bActivate)
    {
        stop_button.bActivate = false;
        isPlaying = false;
        stop();
        play_button.bIsActive = false;
    }

    if(isFading) {
        curTime = ofGetElapsedTimeMillis();
        //if(fadeDirection == 1)
        //{
            //double decay_time = 0.01; // time to fall to ~37% of original amplitude
            //double sample_time = 1.0 / 48000;
            //fadeVolume = exp(- sample_time / decay_time);
        //}
        fadeVolume = fabs(fadeDirection - (curTime - prevTime)/fadeDuration);
        //std::cout << "fade: " << fadeVolume << endl;
        if(curTime - prevTime > fadeDuration)
        {
            endFade();
        }
    }

    for(size_t i=0; i < sounds.size();i++) {
        sounds[i]->fadeVolume = fadeVolume;
        sounds[i]->update();
    }
}

//--------------------------------------------------------------
void Scene::endFade()
{
    if(isFading) {
        isFading = false;
        fadeCallback();
        fadeVolume = 1.0f;
    }
}

//--------------------------------------------------------------
void Scene::enable()
{
    play_button.enableEvents();
    delete_scene.enableEvents();
    stop_button.enableEvents();
    textfield.enable();  
}

//--------------------------------------------------------------
void Scene::disable()
{
    play_button.disableEvents();
    delete_scene.disableEvents();
    stop_button.disableEvents();
    textfield.disable();        
}

//--------------------------------------------------------------
void Scene::enableInteractivity()
{
    if(!bInteractive) {
        //cout << "enable interactivity scene id:" << id << endl;        
        bInteractive = true;
    }
}

//--------------------------------------------------------------
void Scene::disableInteractivity()
{
    if(bInteractive) {        
        bInteractive = false;
    }
}
