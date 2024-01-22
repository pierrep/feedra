#include "SoundObject.h"

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
    if(isSetup) {
        save();
        isSetup = false;
    }
    audioPlayer.stop();
    ofRemoveListener(Interactive::clickedEvent, this, &SoundObject::onClicked);
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
void SoundObject::load()
{
    ofJson json;
    ofFile file("settings/settings"+ofToString(scene_id)+"-"+ofToString(id)+".json");
    if(file.exists()){
        file >> json;
        for(auto & setting: json){
            if(!setting.empty())
            {
                if(!setting["path"].empty()) {
                    soundpath = setting["path"];
                    if(!soundpath.empty()) {
                        bool bLoaded = false;
                        bLoaded = audioPlayer.load(soundpath, isStream);
                        if(bLoaded) {
                            audioPlayer.setLoop(looper.isLooping);
                            player.isLoaded = true;
                        } else {
                            ofLogError() << " Failed to load " << soundpath;
                        }
                    }
                }
                if(!setting["soundname"].empty())
                {
                    soundname.text = setting["soundname"];
                    soundname.enable();
                }
                if(!setting["volume"].empty())
                {
                    volumeslider.setPercent(setting["volume"]);
                }
                if(!setting["loop"].empty())
                {
                    looper.isLooping = setting["loop"];
                }
            }
        }
    }
}

//--------------------------------------------------------------
void SoundObject::save()
{
    settings.clear();

    ofJson loop;
    loop["loop"] = looper.isLooping;
    settings.push_back(loop);

    ofJson filename;
    filename["soundname"] = soundname.text;
    settings.push_back(filename);

    ofJson path;
    path["path"] = soundpath;
    settings.push_back(path);

    ofJson vol;
    vol["volume"] = volumeslider.getValue();
    settings.push_back(vol);

    ofSaveJson("settings/settings"+ofToString(scene_id)+"-"+ofToString(id)+".json",settings);

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
    soundname.disable();

    isSetup = true;

    volumeslider.setup(id,getX(),getY() -20*config->y_scale, 80*config->x_scale, 15*config->y_scale, 0, 1, 0.7, false, false);
    volumeslider.setScale(config->y_scale, config->x_scale);
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
    //ofLogNotice() << "SoundObject id: " << id << " clicked";
    config->activeSound = id;
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

    ofSetColor(64);
    ofSetLineWidth(3);
    ofNoFill();
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);       

    float pos = 0;
    pos = audioPlayer.getPosition();
    //if(id == 1)
    //cout << "pos = " << pos << endl;
    loader.render();
    stopper.render(audioPlayer.isPlaying(),pos);
    player.render(pos);
    looper.render();

    if(player.isPlaying || (player.isLoaded && (pos > 0.0f))) {
        playbar.render(pos);
    }

    if(soundname.isEditing()) {
        ofSetColor(64);
        ofSetLineWidth(1);
        ofDrawRectangle(soundname.bounds);
    }
    ofSetColor(0);
    soundname.draw();

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
            bool bLoaded = audioPlayer.load(result.filePath, isStream);
            if(bLoaded) {
                soundpath = result.filePath;

                filesystem::path s(soundpath);
                config->last_path = s.parent_path().string();

                audioPlayer.setLoop(isLooping);

                soundname.enable();
                soundname.text = result.fileName;
                //soundname.text.resize(14);
                player.isLoaded = true;
            }
        }
        loader.doLoad = false;
    }

    if(player.doPlay)
    {
        if(audioPlayer.isPlaying()) {
            //player.isPlaying = false;
            cout << "pause audio" << id << endl;
            audioPlayer.setPaused(true);
            player.doPlay = false;
            isPaused = true;
        }
        else if (audioPlayer.isLoaded()) {            
            audioPlayer.setPaused(false);
            //audioPlayer.play();
            cout << "play audio" << id << endl;
            player.doPlay = false;
            isPaused = false;
        }
        stopper.isStopped  = false;
    }

    if(stopper.doStop) {
        if(isPaused) {
            audioPlayer.setVolume(0);
            audioPlayer.setPaused(false);
            isPaused = false;
            return;
        }

        if(!stopper.isStopped) {
            {
                stopper.isStopped = true;
                audioPlayer.stop();
            }
        }
        stopper.doStop = false;
        cout << "stop audio" << id << endl;
        //cout << "stop audio" << id <<  " playing: " << audioPlayer.isPlaying() << " position: " << audioPlayer.getPosition() << " player.isPlaying: " << player.isPlaying << endl;
    }

    if(playbar.doScrub) {
        audioPlayer.setPosition(playbar.position);
        //cout << "Scrub to position: " << playbar.position << endl;
        playbar.doScrub = false;
    }

    if(looper.doLooper) {
        looper.doLooper = false;
        audioPlayer.setLoop(looper.isLooping);
    }

    if(audioPlayer.isPlaying()) {
        player.isPlaying = true;
    } else {
        player.isPlaying = false;
    }

    if(audioPlayer.isLoaded())
    {
        audioPlayer.setVolume(volumeslider.getValue()*config->getMasterVolume());
    }

}
