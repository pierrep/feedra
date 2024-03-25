#include "SoundObject.h"

#define STRING_LIMIT 17

ofEvent<size_t> SoundObject::clickedObjectEvent;

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
    if(soundPlayer.isPlaying()) {
        soundPlayer.stop();
    }

    ofRemoveListener(this->clickedEvent, this, &SoundObject::onClicked);
    ofRemoveListener(ofEvents().fileDragEvent, this, &SoundObject::onDragEvent);
    disableAllEvents();
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

    if(player.isLoaded) {
        soundname.disable();
    }
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

    if(player.isLoaded) {
        soundname.enable();
    }
}

//--------------------------------------------------------------
void SoundObject::load()
{
    load("settings/settings.json");
}

//--------------------------------------------------------------
void SoundObject::load(string newpath)
{
    ofJson json;
    ofFile file(newpath);
    string base = "scene"+ofToString(scene_id);
    string prefix = ofToString(scene_id)+"-"+ofToString(id);
    soundpath.clear();

    if(file.exists()){
        file >> json;
        for(auto & json_obj: json){
            if(!json_obj.empty())
            {
                ofJson setting = json_obj[prefix];
                if(!setting.empty()) {
                    if(!setting["isstream"].empty())
                    {
                        isStream = setting["isstream"];
                    }
                    ofJson samples = setting["samples"];
                    for(int i = 0; i < samples.size();i++)
                    {
                        if(!setting["mindelay"].empty())
                        {
                            soundPlayer.minDelay = setting["mindelay"];
                        }
                        if(!setting["maxdelay"].empty())
                        {
                            soundPlayer.maxDelay = setting["maxdelay"];
                        }
                        if(!samples["sample-"+ofToString(i)].empty()) {
                            string new_path = samples["sample-"+ofToString(i)]["path"];
                            soundpath.push_back(new_path);

                            float pitch = samples["sample-"+ofToString(i)]["pitch"];
                            float gain = samples["sample-"+ofToString(i)]["gain"];
                            float pan = samples["sample-"+ofToString(i)]["pan"];

                            if(i > soundPlayer.player.size()-1) {
                                AudioSample s;
                                s.audioPlayer = new OpenALSoundPlayer();
                                s.audioPlayer->setSpeed(pitch);
                                //s.audioPlayer->setVolume(gain);
                                s.audioPlayer->setPan(pan);
                                soundPlayer.player.push_back(s);
                            }
                            bool bLoaded = soundPlayer.load(new_path, i, isStream);
                            if(bLoaded) {
                                //cout << "loaded " << soundpath[i] << endl;
                                soundPlayer.recalculateDelay(i);
                                player.isLoaded = true;
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
                    if(!setting["samplerate"].empty())
                    {
                        sample_rate = setting["samplerate"];
                    }
                    if(!setting["channels"].empty())
                    {
                        channels = setting["channels"];
                    }
                    if(!setting["reverbsend"].empty())
                    {
                        reverbSend = setting["reverbsend"];
                    }
//                    if(!setting["mindelay"].empty())
//                    {
//                        soundPlayer.minDelay = setting["mindelay"];
//                    }
//                    if(!setting["maxdelay"].empty())
//                    {
//                        soundPlayer.maxDelay = setting["maxdelay"];
//                    }
                }
            }
        }
    }

    soundPlayer.setup(config,id);
    soundPlayer.setLoop(looper.isLooping);
    soundPlayer.setReverbSend(reverbSend);
}

//--------------------------------------------------------------
void SoundObject::save()
{
    if(soundpath.empty()) {
        // nothing loaded, don't save
        return;
    }

    string base = "scene"+ofToString(scene_id);
    string prefix = ofToString(scene_id)+"-"+ofToString(id);

    ofJson soundobj;    
    soundobj[base][prefix]["isstream"] = isStream;
    soundobj[base][prefix]["loop"] = looper.isLooping;
    soundobj[base][prefix]["soundname"] = soundname.text;

    for(int i = 0; i < soundpath.size();i++)
    {
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["path"] = soundpath[i];
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["gain"] = 1.0f; // TODO
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["pitch"] = soundPlayer.player[i].audioPlayer->getSpeed();
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["pan"] = soundPlayer.player[i].audioPlayer->getPan();
    }

    soundobj[base][prefix]["volume"] = volumeslider.getValue();
    soundobj[base][prefix]["samplerate"] = sample_rate;
    soundobj[base][prefix]["channels"] = channels;
    //soundobj[base][prefix]["pan"] = soundPlayer.getPan();
    soundobj[base][prefix]["reverbsend"] = soundPlayer.getReverbSend();

    soundobj[base][prefix]["mindelay"] = soundPlayer.minDelay;
    soundobj[base][prefix]["maxdelay"] = soundPlayer.maxDelay;

    //config->settings.push_back(soundobj);
    config->settings.merge_patch(soundobj);

}

//--------------------------------------------------------------
void SoundObject::clear()
{
//    soundname.text = "";
//    isLooping = false;
//    isPaused = false;
//    soundPlayer.stop();
//    soundpath.clear();
//    channels = 0;
//    reverbSend = 0.0f;
//    sample_rate = 0;
//    fadeVolume = 1.0f;
}

//--------------------------------------------------------------
void SoundObject::setup()
{
    ofAddListener(this->clickedEvent, this, &SoundObject::onClicked);    
    ofAddListener(ofEvents().fileDragEvent, this, &SoundObject::onDragEvent);

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
void SoundObject::onDragEvent(ofDragInfo &args)
{
    if(inside(args.position) && (config->activeScene == scene_id)) {
        bool bLoaded = false;

        for(int i = 0; i < args.files.size();i++) {
            ofLogNotice() << "Dropped file onto pad: "<< id << " " << args.files[i];
            string path = args.files[i];
            OpenALSoundPlayer p;
            bool isAudio = p.load(path, isStream);
            if(isAudio) {
                p.unload();
                if(!bLoaded) {
                    soundPlayer.stop();
                    soundPlayer.close();
                    soundpath.clear();
                    bLoaded = true;
                }
                if(i > (int)(soundPlayer.player.size()-1)) {
                    AudioSample s;
                    s.audioPlayer = new OpenALSoundPlayer();
                    soundPlayer.player.push_back(s);
                }
                bool bLoaded = soundPlayer.load(path, i, isStream);
                if(bLoaded) {
                    setupSound(path);
                    soundPlayer.recalculateDelay(i);
                }
            }
        }
        if(bLoaded) {
            //set name to first file
            filesystem::path s(args.files[0]);
            soundname.text = s.stem();
            config->activeSoundIdx = id;
        }
    }
}

//--------------------------------------------------------------
SoundObject::SoundObject(AppConfig* _config, size_t _scene_id, int _id, int _x, int _y, int _w, int _h)
{
    //ofLogNotice() << "SoundObject constructor called, ID = " << _id << " _scene_id = " << _scene_id;
    scene_id = _scene_id;
    isStream = true;
    isSetup = false;
    config = _config;
    channels = 0;
    reverbSend = 0.0f;
    sample_rate = 0;
    fadeVolume = 1.0f;

    id = _id;
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

    config->activeSoundIdx =  id;
    ofLogNotice() << " activeSoundIdx: " << config->activeSoundIdx;

    ofNotifyEvent(clickedObjectEvent,  config->activeSoundIdx);
}

//--------------------------------------------------------------
void SoundObject::render()
{    
    ofPushStyle();

    ofSetHexColor(0xfbe9d8);
    ofFill();
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);

    ofNoFill();
    if(config->activeSoundIdx == id) {
        ofSetLineWidth(4);
        ofSetHexColor(0x65aecd);
    } else {
        ofSetLineWidth(3);
        ofSetColor(32);
    }
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);       

    loader.render();
    stopper.render(soundPlayer.isPlaying(), soundPlayer.getPosition());
    player.render(soundPlayer);
    looper.render();

    //if(player.isPlaying || (player.isLoaded && (pos > 0.0f))) {
    if(player.isPlaying || (player.isLoaded)) {
        playbar.render(soundPlayer);
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
                soundpath.clear();
                setupSound(result.filePath);
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
            cout << "pause audio" << id << endl;
            player.doPlay = false;
            isPaused = true;
            soundPlayer.setPaused(true);
            return;

        }
        else if (soundPlayer.isLoaded()) {            
            cout << "play audio" << id << " channels = " << channels << " samplerate = " << sample_rate << endl;
            soundPlayer.setPaused(false);
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
        soundPlayer.setVolume(volumeslider.getValue()*config->getMasterVolume()*config->masterFade*fadeVolume);
    }

}

void SoundObject::setupSound(string path)
{
    soundpath.push_back(path);

    filesystem::path s(path);
    config->last_path = s.parent_path().string();

    soundPlayer.setLoop(looper.isLooping);

    soundname.enable();
    soundname.text = s.stem();

    sample_rate = soundPlayer.getSampleRate();
    channels = soundPlayer.getNumChannels();

    player.isLoaded = true;
}
