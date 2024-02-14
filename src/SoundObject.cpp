#include "SoundObject.h"

#define STRING_LIMIT 17

ofEvent<int> SoundObject::clickedObjectEvent;

//--------------------------------------------------------------
SoundObject::SoundObject()
{
    isSetup = false;
    id = -1;
    scene_id = 0;
}
//--------------------------------------------------------------
SoundObject::~SoundObject()
{
    soundPlayer.stop();

    ofRemoveListener(this->clickedEvent, this, &SoundObject::onClicked);
    ofRemoveListener(soundname.onTextChange, this, &SoundObject::textFieldEnter);
    //ofLogNotice() << "SoundObject destructor called...ID = " << id;
}

//--------------------------------------------------------------
void SoundObject::disableAllEvents()
{
    disableEvents();
    loader.disableEvents();
    player.disableEvents();
    playbar.disableEvents();
    stopper.disableEvents();
    soundname.disable();
    volumeslider.disableEvents();
    looper.disableEvents();
}

//--------------------------------------------------------------
void SoundObject::enableAllEvents()
{
    enableEvents();
    loader.enableEvents();
    player.enableEvents();
    playbar.enableEvents();
    stopper.enableEvents();
    soundname.enable();
    volumeslider.enableEvents();
    looper.enableEvents();
}

//--------------------------------------------------------------
void SoundObject::load(int idx)
{
    ofJson json;
    ofFile file("settings/settings.json");
    string prefix = ofToString(scene_id)+"-"+ofToString(id);
    if(file.exists()){
        file >> json;
        for(auto & setting: json){
            if(!setting.empty())
            {
                if(!setting[prefix+"-path"].empty()) {
                    soundpath = setting[prefix+"-path"];
                    if(!soundpath.empty()) {
                        bool bLoaded = false;
                        bLoaded = soundPlayer.load(soundpath, isStream);
                        if(bLoaded) {
                            soundPlayer.setLoop(looper.isLooping);
                            //channels = soundPlayer.getNumChannels();
                            player.isLoaded = true;
                        } else {
                            ofLogError() << " Failed to load " << soundpath;
                        }
                    }
                }
                if(!setting[prefix+"-soundname"].empty())
                {
                    soundname.text = setting[prefix+"-soundname"];
                    soundname.enable();
                }
                if(!setting[prefix+"-volume"].empty())
                {
                    volumeslider.setPercent(setting[prefix+"-volume"]);
                }
                if(!setting[prefix+"-loop"].empty())
                {
                    looper.isLooping = setting[prefix+"-loop"];                    
                }
                if(!setting[prefix+"-samplerate"].empty())
                {
                    sample_rate = setting[prefix+"-samplerate"];
                }
                if(!setting[prefix+"-channels"].empty())
                {
                    channels = setting[prefix+"-channels"];
                }
                if(!setting[prefix+"-pan"].empty())
                {
                    pan = setting[prefix+"-pan"];
                }
                if(!setting[prefix+"-mindelay"].empty())
                {
                    soundPlayer.minDelay[0] = setting[prefix+"-mindelay"];
                }
                if(!setting[prefix+"-maxdelay"].empty())
                {
                    soundPlayer.maxDelay[0] = setting[prefix+"-maxdelay"];
                }
            }
        }
    }

    soundPlayer.setup(config,id);
    soundPlayer.setLoop(looper.isLooping);
    soundPlayer.setPan(pan);
}

//--------------------------------------------------------------
void SoundObject::save()
{
    if(soundpath.empty()) {
        // nothing loaded, don't save
        return;
    }

    string prefix = ofToString(scene_id)+"-"+ofToString(id);

    ofJson soundobj;
    soundobj[prefix+"-loop"] = looper.isLooping;
    soundobj[prefix+"-soundname"] = soundname.text;
    soundobj[prefix+"-path"] = soundpath;
    soundobj[prefix+"-volume"] = volumeslider.getValue();
    soundobj[prefix+"-samplerate"] = sample_rate;
    soundobj[prefix+"-channels"] = channels;
    soundobj[prefix+"-pan"] = soundPlayer.getPan();
    soundobj[prefix+"-mindelay"] = soundPlayer.minDelay[0];
    soundobj[prefix+"-maxdelay"] = soundPlayer.maxDelay[0];

    config->settings.push_back(soundobj);

}

//--------------------------------------------------------------
void SoundObject::setup()
{
    ofAddListener(this->clickedEvent, this, &SoundObject::onClicked);
    ofAddListener(soundname.onTextChange, this, &SoundObject::textFieldEnter);

    loader.setup();
    stopper.setup();
    player.setup(config);
    playbar.setup(config);
    looper.setup(config);
    soundname.setUseListeners(true);
    soundname.setStringLimit(STRING_LIMIT);
    soundname.disable();

    isSetup = true;

    volumeslider.setup(id,getX(),getY() -20*config->y_scale, 80*config->x_scale, 15*config->y_scale, 0, 1, 0.7, false, false);
    volumeslider.setScale(config->y_scale, config->x_scale);
    volumeslider.setFont(&config->f2());
    //\volumeslider.setLabelString("volume");
}

//--------------------------------------------------------------
SoundObject::SoundObject(AppConfig* _config, size_t _scene_id, int _id, int _x, int _y, int _w, int _h)
{
    //ofLogNotice() << "SoundObject constructor called, ID = " << _id;
    scene_id = _scene_id;
    isStream = true;
    isSetup = false;
    config = _config;
    channels = 0;
    sample_rate = 0;

    id = _id + _scene_id*100;
    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
    isLooping = config->loopByDefault;

    loader.id = _id;
    loader.setX(_x + 10 * config->x_scale);
    loader.setY(_y + 10 * config->y_scale);
    loader.setWidth(15 * config->x_scale);
    loader.setHeight(15 * config->y_scale);

    player.id = _id;
    player.setX(_x + 35 * config->x_scale);
    player.setY(_y + 35 * config->y_scale);
    player.setWidth(50 * config->x_scale);
    player.setHeight(50 * config->y_scale);

    playbar.id = _id;
    playbar.setX(player.getX());
    playbar.setY(player.getY()+player.getHeight() + 1);
    playbar.setWidth(player.getWidth());
    playbar.setHeight(10*config->y_scale);

    stopper.id = _id;
    stopper.setWidth(15 * config->x_scale);
    stopper.setHeight(15 * config->y_scale);
    stopper.setX(_x + _w - stopper.getWidth() - 10 * config->x_scale);
    stopper.setY(_y + 10 * config->y_scale);

    looper.id = _id;
    looper.setWidth(15 * config->x_scale);
    looper.setHeight(15 * config->y_scale);
    looper.setX(_x + _w - getHeight()/2.0f - looper.getWidth()/2.0f);
    looper.setY(_y + 10 * config->y_scale);

    soundname.text = "";
    soundname.setFont(config->f2());
    soundname.bounds = ofRectangle(_x + 10* config->x_scale, _y + 100* config->y_scale, 100*config->x_scale, 16*config->y_scale);
}

//--------------------------------------------------------------
SoundObject::SoundObject(const SoundObject& parent) {
    //ofLogNotice() << "SoundObject copy constructor called, ID = "<< parent.id;
    isStream = parent.isStream;
    isSetup = parent.isSetup;
    config = parent.config;
    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
    isLooping = parent.isLooping;

    loader.id = parent.loader.id;
    loader.setX(parent.loader.x);
    loader.setY(parent.loader.y);
    loader.setWidth(parent.loader.width);
    loader.setHeight(parent.loader.height);

    player.id = parent.player.id;
    player.setX(parent.player.x);
    player.setY(parent.player.y);
    player.setWidth(parent.player.width);
    player.setHeight(parent.player.height);

    playbar.id = parent.playbar.id;
    playbar.setX(parent.playbar.x);
    playbar.setY(parent.playbar.y);
    playbar.setWidth(parent.playbar.width);
    playbar.setHeight(parent.playbar.height);

    soundname = parent.soundname;
    soundname.setFont(config->f2());
}

//--------------------------------------------------------------
void SoundObject::onClicked(int& args) {
    ofLogNotice() << "SoundObject id " << id << " clicked";

    config->activeSound = id;
    int idx = id - config->activeSceneIdx*100 -1;
    config->activeSoundIdx =  idx;
    ofLogNotice() << "activeSound: " << config->activeSound << " activeSoundIdx: " << config->activeSoundIdx;

    ofNotifyEvent(clickedObjectEvent, idx);
}

//--------------------------------------------------------------
void SoundObject::textFieldEnter(string& newText){

    //cout << "new text =  " << newText << endl;
}

//--------------------------------------------------------------
void SoundObject::render()
{    
    ofPushStyle();

    ofSetHexColor(0xfbe9d8);
    ofFill();
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);

    ofNoFill();
    if(config->activeSound == id) {
        ofSetLineWidth(4);
        ofSetHexColor(0x65aecd);
    } else {
        ofSetLineWidth(3);
        ofSetColor(32);
    }
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);       

    float pos = 0;
    pos = soundPlayer.getPosition();
    //if(id == 1)
    //cout << "pos = " << pos << endl;
    loader.render();
    stopper.render(soundPlayer.isPlaying(), pos);
    player.render(soundPlayer.isPlayingDelay(),pos);
    looper.render();

    //if(player.isPlaying || (player.isLoaded && (pos > 0.0f))) {
    if(player.isPlaying || (player.isLoaded)) {
        playbar.render(soundPlayer.isPlayingDelay(),pos);
    }

    if(soundname.isEditing()) {
        ofSetColor(64);
        ofSetLineWidth(1);
        ofDrawRectangle(soundname.bounds);
    }
    ofSetColor(0);
    soundname.draw();

    volumeslider.render();

    ofPopStyle();
}

//--------------------------------------------------------------
void SoundObject::update()
{
    if(loader.doLoad) {
        ofFileDialogResult result;
        string path = config->getLibraryLocation();
        if(config->last_path.compare("") != 0)
        {
            path = config->last_path;
        }

        result = ofSystemLoadDialog("load file",false,path);
        if(result.bSuccess) {           
            bool bLoaded = soundPlayer.load(result.filePath, isStream);
            if(bLoaded) {
                soundpath = result.filePath;

                filesystem::path s(soundpath);
                config->last_path = s.parent_path().string();

                soundPlayer.setLoop(isLooping);

                soundname.enable();
                soundname.text = result.fileName;

                sample_rate = soundPlayer.getSampleRate();
                channels = soundPlayer.getNumChannels();
                player.isLoaded = true;
            }
        }
        loader.doLoad = false;
    }

    if(!player.isLoaded)
    {
        //do not proceed if we have no file loaded
        return;
    }

    soundPlayer.update();

    if(player.doPlay)
    {
        if(soundPlayer.isPlaying()) {
            //player.isPlaying = false;
            cout << "pause audio" << id << endl;
            soundPlayer.setPaused(true);
            player.doPlay = false;
            isPaused = true;
        }
        else if (soundPlayer.isLoaded()) {
            soundPlayer.setPaused(false);
            //audioPlayer.play();
            cout << "play audio" << id << " channels = " << channels << " samplerate = " << sample_rate << endl;
            player.doPlay = false;
            isPaused = false;
        }
        stopper.isStopped  = false;
    }

    if(stopper.doStop) {
        if(isPaused) {
            soundPlayer.setVolume(0);
            soundPlayer.setPaused(false);
            isPaused = false;
            return;
        }

        if(!stopper.isStopped) {
            {
                stopper.isStopped = true;
                soundPlayer.stop();
            }
        }
        stopper.doStop = false;
        cout << "stop audio" << id << endl;
        //cout << "stop audio" << id <<  " playing: " << audioPlayer.isPlaying() << " position: " << audioPlayer.getPosition() << " player.isPlaying: " << player.isPlaying << endl;
    }

    if(playbar.doScrub) {
        soundPlayer.setPosition(playbar.position);
        //cout << "Scrub to position: " << playbar.position << endl;
        playbar.doScrub = false;
    }

    if(looper.doLooper) {
        looper.doLooper = false;
        soundPlayer.setLoop(looper.isLooping);
    }

    if(soundPlayer.isPlaying()) {
        player.isPlaying = true;
    } else {
        player.isPlaying = false;
    }

    if(soundPlayer.isLoaded())
    {
        soundPlayer.setVolume(volumeslider.getValue()*config->getMasterVolume()*config->masterFade);
    }

}
