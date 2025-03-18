#include "SoundObject.h"
#include "DialogUtils.h"

#define STRING_LIMIT 17

ofEvent<size_t> SoundObject::clickedObjectEvent;
ofEvent<size_t> SoundObject::releasedObjectEvent;
ofEvent<size_t> SoundObject::draggedObjectEvent;

//--------------------------------------------------------------
SoundObject::SoundObject()
{
    isSetup = false;
    id = -1;
    scene_id = 0;
}

//--------------------------------------------------------------
SoundObject::SoundObject(AppConfig* _config, size_t _scene_id, int _id, int _x, int _y, int _w, int _h)
{
    //ofLogNotice() << "SoundObject constructor called, ID = " << _id << " _scene_id = " << _scene_id;
    scene_id = _scene_id;
    isStream = true;
    isSetup = false;
    bLoading = false;
    config = _config;
    channels = 0;
    reverbSend = 0.0f;
    sample_rate = 0;
    fadeVolume = 1.0f;
    soundPlayer.config = _config;

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

    playButton.id = _id;
    playButton.setX(_x + 35 * config->x_scale);
    playButton.setY(_y + 35 * config->y_scale);
    playButton.setWidth(50 * config->x_scale);
    playButton.setHeight(50 * config->y_scale);

    playbar.id = _id;
    playbar.setX(playButton.getX());
    playbar.setY(playButton.getY()+playButton.getHeight() + 1);
    playbar.setWidth(playButton.getWidth());
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
SoundObject::~SoundObject()
{    
    try {
        if(getNativeThread().joinable()) {
            getNativeThread().join();
        }
    } catch (const std::exception &exc) {
        cout << "exception called:" << exc.what() <<  endl;
    }
    ofRemoveListener(ofEvents().fileDragEvent, this, &SoundObject::onDragEvent);
    ofRemoveListener(this->clickedEvent, this, &SoundObject::onClicked);
    ofRemoveListener(this->releasedEvent, this, &SoundObject::onReleased);
    ofRemoveListener(this->draggedEvent, this, &SoundObject::onDragged);
    disableAllEvents();
    //ofLogNotice() << "SoundObject destructor called...ID = " << id;
}

//--------------------------------------------------------------
void SoundObject::setup()
{
    ofAddListener(ofEvents().fileDragEvent, this, &SoundObject::onDragEvent);
    ofAddListener(this->clickedEvent, this, &SoundObject::onClicked);
    ofAddListener(this->releasedEvent, this, &SoundObject::onReleased);
    ofAddListener(this->draggedEvent, this, &SoundObject::onDragged);

    loader.setup();
    stopper.setup();
    playButton.setup(config);
    playbar.setup(config);
    looper.setup(config);
    soundname.setUseListeners(true);
    soundname.setStringLimit(STRING_LIMIT);
    soundname.setTextAlignment(TextInputField::TextAlignment::CENTRE);
    soundname.disable();

    isSetup = true;

    volumeslider.setup(id,getX(),getY() -20*config->y_scale, 80*config->x_scale, 15*config->y_scale, 0, 1, 0.7, false, false);
    volumeslider.setScale(config->y_scale, config->x_scale);
    volumeslider.setFont(&config->f2());
    //\volumeslider.setLabelString("volume");

    disableAllEvents();
}

//--------------------------------------------------------------
void SoundObject::enableEditorMode()
{
    for(int i = 0; i< soundPlayer.player.size();i++)
    {
        soundPlayer.player.at(i)->enableEditorMode();
    }
}

//--------------------------------------------------------------
void SoundObject::disableEditorMode()
{
    for(int i = 0; i< soundPlayer.player.size();i++)
    {
        soundPlayer.player.at(i)->disableEditorMode();
    }
}

//--------------------------------------------------------------
void SoundObject::disableAllEvents()
{
    disableEvents();
    loader.disableEvents();
    playButton.disableEvents();
    playbar.disableEvents();
    stopper.disableEvents();
    volumeslider.disableEvents();
    looper.disableEvents();

    if(playButton.isLoaded) {
        soundname.disable();
    }
}

//--------------------------------------------------------------
void SoundObject::enableAllEvents()
{
    enableEvents();
    loader.enableEvents();
    playButton.enableEvents();
    playbar.enableEvents();
    stopper.enableEvents();
    volumeslider.enableEvents();
    looper.enableEvents();

    if(playButton.isLoaded) {
        soundname.enable();
    }
}

//--------------------------------------------------------------
void SoundObject::threadedFunction()
{
    if(alcIsExtensionPresent(OpenALSoundPlayer::getCurrentDevice(), "ALC_EXT_thread_local_context"))
    {
        ALCboolean (ALC_APIENTRY*alcSetThreadContext)(ALCcontext* context);
        alcSetThreadContext = reinterpret_cast<ALCboolean (ALC_APIENTRY*)(ALCcontext *context)>(alcGetProcAddress(OpenALSoundPlayer::getCurrentDevice(), "alcSetThreadContext"));
        alcSetThreadContext(main_context);

        setup();
        load();

        alcSetThreadContext(NULL);
    }
    bLoading = false;

}

//--------------------------------------------------------------
//void SoundObject::loadThreaded()
//{
//    loadThreaded("settings/settings.json");
//}

//--------------------------------------------------------------
void SoundObject::loadThreaded()
{
    bool bDoThreading = alcIsExtensionPresent(OpenALSoundPlayer::getCurrentDevice(), "ALC_EXT_thread_local_context");
    if(bDoThreading) {
        main_context = alcGetCurrentContext();
        bLoading = true;
        //newpath = _newpath;
        startThread();
    }
}

////--------------------------------------------------------------
//void SoundObject::load()
//{
//    load("settings/settings.json");
//}

//--------------------------------------------------------------
void SoundObject::load()
{
    string base = "scene"+ofToString(scene_id);
    string prefix = ofToString(scene_id)+"-"+ofToString(id);
    soundpath.clear();
    for(auto & json_obj: config->json){
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
                        std::filesystem::path np = samples["sample-" + ofToString(i)]["path"];
                        string new_path = np.make_preferred().string();
                        soundpath.push_back(new_path);

                        float pitch = samples["sample-"+ofToString(i)]["pitch"];                            
                        float gain = samples["sample-"+ofToString(i)]["gain"];
                        float pan = samples["sample-"+ofToString(i)]["pan"];
                        bool panrandom = false;
                        if (!samples["sample-" + ofToString(i)]["panrandom"].empty()) {
                            panrandom = samples["sample-" + ofToString(i)]["panrandom"];
                        }

                        if(i > (int)soundPlayer.player.size()-1) {
                            AudioSample* s = new AudioSample();
                            s->audioPlayer = new OpenALSoundPlayer();
                            s->config = config;
                            s->id = soundPlayer.player.size();
                            s->setWidth(config->sample_gui_width);
                            s->setHeight(35*config->y_scale);
                            soundPlayer.player.push_back(s);
                        }
                        bool bLoaded = soundPlayer.load(new_path, i, isStream);
                        if(bLoaded) {
                            //cout << "loaded " << soundpath[i] << " id = "<< soundPlayer.player[i]->id << endl;
                            soundPlayer.recalculateDelay(i);
                            soundPlayer.player[i]->sample_path = new_path;
                            soundPlayer.player[i]->setPitch(pitch);
                            soundPlayer.player[i]->setGain(gain);
                            soundPlayer.player[i]->setPan(pan);
                            soundPlayer.setRandomPan(panrandom);
                            soundPlayer.player[i]->setup();
                            playButton.isLoaded = true;
                        }
                    }

                }
                if(!setting["soundname"].empty())
                {
                    soundname.text = setting["soundname"];
                    soundname.disable();
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
                if(!setting["mindelay"].empty())
                {
                    soundPlayer.minDelay = setting["mindelay"];
                }
                if(!setting["maxdelay"].empty())
                {
                    soundPlayer.maxDelay = setting["maxdelay"];
                }
                if(!setting["playrandom"].empty())
                {
                    soundPlayer.bRandomPlayback = setting["playrandom"];
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
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["gain"] = soundPlayer.player[i]->getGain();
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["pitch"] = soundPlayer.player[i]->getPitch();
        soundobj[base][prefix]["samples"]["sample-"+ofToString(i)]["pan"] = soundPlayer.player[i]->audioPlayer->getPan();
        soundobj[base][prefix]["samples"]["sample-" + ofToString(i)]["duration"] = soundPlayer.player[i]->audioPlayer->getDuration();
    }

    soundobj[base][prefix]["volume"] = volumeslider.getValue();
    soundobj[base][prefix]["samplerate"] = sample_rate;
    soundobj[base][prefix]["channels"] = channels;
    soundobj[base][prefix]["playrandom"] = soundPlayer.bRandomPlayback;
    soundobj[base][prefix]["reverbsend"] = soundPlayer.getReverbSend();

    soundobj[base][prefix]["mindelay"] = soundPlayer.minDelay;
    soundobj[base][prefix]["maxdelay"] = soundPlayer.maxDelay;

    //config->settings.push_back(soundobj);
    config->settings.merge_patch(soundobj);

}

//--------------------------------------------------------------
void SoundObject::onDragEvent(ofDragInfo &args)
{
    if(inside(args.position) && (config->activeScene == scene_id)) {
        loadMultipleSounds(args.files,true);
    }
}

//--------------------------------------------------------------
void SoundObject::loadMultipleSounds(vector<string>& filePaths, bool bClearSounds)
{
    bool bLoaded = false;
    for(int i = 0; i < filePaths.size();i++) {
        bLoaded = loadSingleSound(filePaths.at(i),bClearSounds);
        if(bLoaded) {
            bClearSounds = false;
        }
    }
    if(bLoaded) {
        if(soundname.text.empty()) {
            //set name to first file
            filesystem::path s(filePaths.at(0));
            soundname.text = s.stem().string();
            soundname.enable();
        }
        config->activeSoundIdx = id;
    }
}

//--------------------------------------------------------------
bool SoundObject::loadSingleSound(std::string filepath, bool bClearSounds)
{
    ofLogNotice() << "Loaded file onto pad: "<< id << " " << filepath;
    string path = filepath;
    OpenALSoundPlayer p;
    bool bIsAudio = p.load(path, isStream);
    if(bIsAudio) {
        p.unload();
        if(bClearSounds) {
            soundPlayer.stop();
            soundPlayer.close();
            soundpath.clear();
            bClearSounds = false;
        }

        AudioSample* s = new AudioSample();
        s->audioPlayer = new OpenALSoundPlayer();
        s->config = config;

        int maxid = 0;
        for(int i=0; i < soundPlayer.player.size();i++) {
            if(soundPlayer.player.at(i)->id > maxid) {
                maxid = soundPlayer.player.at(i)->id;
            }
        }
        s->id = maxid + 1;

        s->setWidth(config->sample_gui_width);
        s->setHeight(35*config->y_scale);
        bool bLoaded = s->audioPlayer->load(path,isStream);
        if(bLoaded) {            
            s->setup();
            s->sample_path = path;
            soundPlayer.player.push_back(s);
            setupSound(path);
            size_t idx = soundPlayer.player.size()-1;
            soundPlayer.recalculateDelay(idx);
        } else {
            delete s;
        }
    }
    return bIsAudio;
}

//--------------------------------------------------------------
void SoundObject::onClicked(ClickArgs& args) {
    if(!bEventsEnabled) {
        ofLogNotice() << "clicked sound obj " << id << " , but bEventsEnabled is false ";
        return;
    }

    config->activeSoundIdx =  id;

    ofNotifyEvent(clickedObjectEvent,  config->activeSoundIdx);
}

//--------------------------------------------------------------
void SoundObject::onDragged(ClickArgs& args) {
    if(!bEventsEnabled) return;

    config->prevSoundIdx = config->activeSoundIdx;

    ofNotifyEvent(draggedObjectEvent,  config->activeSoundIdx);
}

//--------------------------------------------------------------
void SoundObject::onReleased(ClickArgs& args) {
    if(!bEventsEnabled) return;

    config->activeSoundIdx =  id;

    ofNotifyEvent(releasedObjectEvent,  config->activeSoundIdx);
}

//--------------------------------------------------------------
void SoundObject::render()
{    
    if(bLoading) return;

    ofPushStyle();

    ofSetHexColor(0xfbe9d8);
    ofFill();
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),config->x_scale*5);

    ofNoFill();
    if(config->activeSoundIdx == id) {
        ofSetLineWidth(4*config->x_scale);
        ofSetHexColor(0x65aecd);
    } else {
        ofSetLineWidth(3*config->x_scale);
        ofSetColor(32);
    }
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),config->x_scale*5);

    loader.render();
    stopper.render(soundPlayer.isPlaying(), soundPlayer.getPosition());
    playButton.render(soundPlayer);
    looper.render();

    //if(player.isPlaying || (player.isLoaded && (pos > 0.0f))) {
    if(playButton.isPlaying || (playButton.isLoaded)) {
        playbar.render(soundPlayer);
    }

    if(soundname.isEditing()) {
        ofSetColor(64);
        ofSetLineWidth(1*config->x_scale);
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
        ofFileDialogResultMulti result;    
        string path = config->getLibraryLocation();
        if(config->last_path.compare("") != 0){
            path = config->last_path;
        }

        result = ofSystemLoadDialogMulti("load files",false,true,path);
        if(result.bSuccess) {           
            loadMultipleSounds(result.filePaths,true);
        }
        loader.doLoad = false;
    }

    if(!playButton.isLoaded)
    {
        //do not proceed if we have no file loaded
        return;
    }

    soundPlayer.update();

    if(playButton.doPlay)
    {
        if(soundPlayer.isPlaying()) {
            cout << "pause audio" << id << endl;
            playButton.doPlay = false;
            isPaused = true;
            soundPlayer.setPaused(true);
            return;

        }
        else if (soundPlayer.isLoaded()) {            
            cout << "play audio" << id << " channels = " << channels << " samplerate = " << sample_rate << endl;
            soundPlayer.setPaused(false);
            playButton.doPlay = false;
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
        playButton.isPlaying = true;
    } else {
        playButton.isPlaying = false;
    }

    if(soundPlayer.isLoaded())
    {
        soundPlayer.setVolume(volumeslider.getValue()*config->getMasterVolume()*config->masterFade*fadeVolume*soundPlayer.player.at(soundPlayer.getCurSound())->getGain());
    }

}

void SoundObject::setupSound(string path)
{
    soundpath.push_back(path);

    filesystem::path s(path);
    config->last_path = s.parent_path().string();

    soundPlayer.setLoop(looper.isLooping);

    sample_rate = soundPlayer.getSampleRate();
    channels = soundPlayer.getNumChannels();

    playButton.isLoaded = true;
}
