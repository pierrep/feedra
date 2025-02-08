#include "ofApp.h"
#include "GLFW/glfw3.h"

#include "AL/alext.h"

bool ofApp::bMinimised = false;

//--------------------------------------------------------------
ofApp::~ofApp()
{
    ofRemoveListener(panSlider.clickedEvent, this, &ofApp::onSliderClicked);   
    ofRemoveListener(pitchSlider.clickedEvent, this, &ofApp::onSliderClicked);
    ofRemoveListener(gainSlider.clickedEvent, this, &ofApp::onSliderClicked);

    ofRemoveListener(reverbSend.clickedEvent, this, &ofApp::onSliderClicked);
    ofRemoveListener(minDelay.numberChangedEvent, this, &ofApp::onNumberChanged);
    ofRemoveListener(maxDelay.numberChangedEvent, this, &ofApp::onNumberChanged);
    ofRemoveListener(randomPlayback.clickedEvent, this, &ofApp::onCheckboxClicked);

    ofRemoveListener(SoundObject::clickedObjectEvent, this, &ofApp::onObjectClicked);
    ofRemoveListener(AudioSample::clickedSampleEvent, this, &ofApp::onSampleClicked);

    saveConfig();

    for(size_t i=0;i < scenes.size();i++) {        
        delete scenes[i];
    }
    scenes.clear();

    delete addScene;
}

//--------------------------------------------------------------
void ofApp::saveConfig()
{
    string path = ofToDataPath("settings/settings.json",true);
    saveConfig(path, false);
}

//--------------------------------------------------------------
void ofApp::saveConfig(string newpath, bool bCopyFiles)
{
    if(newpath == "")
    {
        ofLogError() << "Empty path, config not saved!";
        return;
    }
    config.settings.clear();

    ofJson globalsettings;    
    globalsettings["global"]["mainvolume"] = mainVolume.getValue();
    globalsettings["global"]["maxscenes"] = scenes.size();
    globalsettings["global"]["activesceneid"] = config.activeSceneIdx;
    config.settings.merge_patch(globalsettings);

    ofJson sceneInfo;
    for(size_t i=0;i < scenes.size();i++) {
        // save scenes
        sceneInfo["scene"+ofToString(i)]["id"] = scenes[i]->id;
        sceneInfo["scene"+ofToString(i)]["name"] = scenes[i]->textfield.text;
        sceneInfo["scene"+ofToString(i)]["activesound"] = scenes[i]->activeSoundIdx;

        string rootdir;
        if(bCopyFiles) {
            filesystem::path np(newpath);
            rootdir = np.parent_path().string() + "/files/";
            try
            {
                std::filesystem::create_directory(rootdir);
            }
            catch (std::exception& e)
            {
                ofLogError() << "Failed to create directory, error: " << e.what() << endl;
                return;
            }
        }

        //save sounds
        for(int j=0;j < scenes[i]->sounds.size();j++)
        {       
            if(bCopyFiles) {
                for(int k = 0; k < scenes[i]->sounds[j]->soundpath.size();k++) {
                    string p = scenes[i]->sounds[j]->soundpath[k];
                    filesystem::path curpath(p);
                    string name = curpath.filename().generic_string();
                    string d = rootdir + name;
                    std::filesystem::path destpath(d);

                    if (curpath == destpath) {
                        // We currently don't overwrite files in the same directory...probably we should but it's faster to only add new files and new config
                        ofLogVerbose() << "Same file path, skipping";
                        break;
                    }                    
                    try
                    {
                        std::filesystem::copy_file(curpath,destpath,std::filesystem::copy_options::update_existing);
                        ofLogVerbose() << "File copied: " << destpath.string();
                    }
                    catch (std::exception& e)
                    {
                        ofLogError() << "Result : " << e.what();
                    }
                    scenes[i]->sounds[j]->soundpath[k] = destpath.generic_string();
                }
                scenes[i]->sounds[j]->save();
            } 
            else 
            {
                scenes[i]->sounds[j]->save();
            }

        }
    }
    config.settings.merge_patch(sceneInfo);

    filesystem::path p(newpath);
    if(p.extension() != ".json") {
        newpath = (string) p.parent_path().string() + "/" + (string)p.stem().string() + ".json";
    }
    string save_name = newpath;
    ofSavePrettyJson(save_name,config.settings);
}

//--------------------------------------------------------------
void ofApp::loadConfig()
{
    loadConfig("settings/settings.json");
}

//--------------------------------------------------------------
void ofApp::loadConfig(string newpath)
{
    ofJson setting;
    ofFile file(newpath);

    if(file.exists() && (file.getExtension() == "json")){
    file >> setting;
    if(!setting.empty())
    {
        if(!setting["global"]["maxscenes"].empty())
        {
            maxScenes = setting["global"]["maxscenes"];
        }
        if(!setting["global"]["activesceneid"].empty())
        {
            config.activeSceneIdx = setting["global"]["activesceneid"];
        }
        if(!setting["global"]["mainvolume"].empty())
        {
            float v = setting["global"]["mainvolume"];
            mainVolume.setPercent(v);
        }
    }

    if(maxScenes > 0) {
        if(!setting.empty())
        {
            for(size_t i=0;i < scenes.size();i++) {
                delete scenes[i];
            }
            scenes.clear();

            for(int i=0; i < maxScenes;i++) {
                if(!setting["scene"+ofToString(i)]["id"].empty())
                {
                    int new_scene_id = setting["scene"+ofToString(i)]["id"];
                    //cout << "new_scene_id: " << new_scene_id << endl;

                    int x = config.baseSceneOffset;
                    int y = i* config.scene_spacing + config.scene_yoffset;
                    string name = "";
                    if(!setting["scene"+ofToString(i)]["name"].empty())
                    {
                        name = setting["scene"+ofToString(i)]["name"];
                    }
                    int asid = 1;
                    if(!setting["scene"+ofToString(i)]["activesound"].empty())
                    {
                        asid = setting["scene"+ofToString(i)]["activesound"];
                    }
                    Scene* s = new Scene(&config,name,new_scene_id,asid,x,y,config.scene_width,config.scene_height);
                    scenes.push_back(s);
                }
            }
        }
    }

    } else {
        ofLogWarning() << newpath << " is not a Feedra json configuation file, creating default config";
    }
}

//--------------------------------------------------------------
void ofApp::calculateSources()
{
    int numMonoSources = 0;
    int numStereoSources = 0;

    for(size_t i=0;i < scenes.size();i++) {

        //save sounds
        for(int j=0;j < scenes[i]->sounds.size();j++)
        {
            size_t num = scenes[i]->sounds[j]->soundPlayer.player.size();
            for(size_t k=0;k < num;k++) {
                OpenALSoundPlayer* p = scenes[i]->sounds[j]->soundPlayer.player[k]->audioPlayer;
                ALenum fmt = p->getOpenALFormat();
                if(fmt == AL_FORMAT_STEREO16 || fmt == AL_FORMAT_STEREO_FLOAT32) {
                    numStereoSources += p->getNumSources();
                } else {
                    numMonoSources += p->getNumSources();
                }
            }
        }
    }
    ofLogNotice() << "Current num mono sources = " << numMonoSources;
    ofLogNotice() << "Current num stereo sources = " << numStereoSources;
}

//--------------------------------------------------------------
void ofApp::window_minimise_callback(GLFWwindow* window, int minimised)
{
    if (minimised)
    {
        // The window was minimised
    }
    else
    {
        // The window was restored
    }
    bMinimised = minimised;
    cout << "bMinimised: " <<  minimised << endl;
}

//--------------------------------------------------------------
void ofApp::setup(){
    ofSetEscapeQuitsApp(false);
    ofSetWindowTitle("Feedra");    
    //ofSetVerticalSync(true);
    ofSetFrameRate(30); // the play and stop buttons flicker on sample switch or loop at 60fps

    ofFile file("settings/settings.json");
    if(file.exists()){
        file.copyTo("settings/settings_backup.json",true,true);
    }

    if( ofAppGLFWWindow* glfwWin = dynamic_cast< ofAppGLFWWindow* >(ofGetCurrentWindow().get()) ){
        ofGLFWWindowSettings GLFWsettings = glfwWin->getSettings();
        ofLogNotice() << "OpenGL version = " << GLFWsettings.glVersionMajor << "." << GLFWsettings.glVersionMinor;
        ofLogNotice() << "GLFW version : " << GLFW_VERSION_MAJOR << "." << GLFW_VERSION_MINOR << " rev " << GLFW_VERSION_REVISION;
        ofLogNotice() << "getPixelScreenCoordScale = " << glfwWin->getPixelScreenCoordScale();

        glfwGetWindowContentScale(glfwWin->getGLFWWindow(),&config.x_scale,&config.y_scale);
        ofLogNotice() << "glfwGetWindowContentScale  x = " << config.x_scale << " y = " << config.y_scale;

        glfwSetWindowIconifyCallback(glfwWin->getGLFWWindow(),this->window_minimise_callback);
        ofSetWindowShape(ofGetWindowWidth()*config.x_scale,ofGetWindowHeight()*config.y_scale);
    }

    // Set the current default audio device
    curDevice = newDevice = OpenALSoundPlayer::getDefaultDevice();

    bDoRender = true;
    bLoadScenes = false;
    bClearPad = false;
    pageState = PageState::MAIN;

    config.setup();

    mainVolume.setup(-1,config.xoffset,20*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,1,false,false);
    mainVolume.setScale(config.y_scale, config.x_scale);
    mainVolume.setFont(&config.f2());
    mainVolume.setLabelString("Main Volume");

    minDelay.setup(&config,1,285*config.x_scale,ofGetHeight() - 140*config.y_scale);
    minDelay.setFont(&config.f2());
    minDelay.setLabelString("Min delay");

    maxDelay.setup(&config,2,445*config.x_scale,ofGetHeight() - 140*config.y_scale);
    maxDelay.setFont(&config.f2());
    maxDelay.setLabelString("Max delay");

    reverbSend.setup(4,config.xoffset+600*config.x_scale,ofGetHeight() - 135*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,0,false,false);
    reverbSend.setScale(config.y_scale, config.x_scale);
    reverbSend.setFont(&config.f2());
    reverbSend.setLabelString("Reverb send");

    //Edit sliders
    panSlider.setup(3,config.xoffset+800*config.x_scale,ofGetHeight() - 160*config.y_scale,200*config.x_scale,20*config.y_scale,-1,1,0,false,false);
    panSlider.setScale(config.y_scale, config.x_scale);
    panSlider.setFont(&config.f2());
    panSlider.setLabelString("Panning");

    pitchSlider.setup(5,config.xoffset+800*config.x_scale, ofGetHeight() - 120*config.y_scale,200*config.x_scale,20*config.y_scale,0,3,0,false,false);
    pitchSlider.setScale(config.y_scale, config.x_scale);
    pitchSlider.setFont(&config.f2());
    pitchSlider.setLabelString("Pitch");
    pitchSlider.disableEvents();

    gainSlider.setup(6,config.xoffset+800*config.x_scale, ofGetHeight() - 80*config.y_scale,200*config.x_scale,20*config.y_scale,1,6,1,false,false);
    gainSlider.setScale(config.y_scale, config.x_scale);
    gainSlider.setFont(&config.f2());
    gainSlider.setLabelString("Gain");
    gainSlider.disableEvents();

    //Checkbox
    randomPlayback.setX(config.xoffset+600*config.x_scale);
    randomPlayback.setY(ofGetHeight() - 70*config.y_scale);
    randomPlayback.setWidth(20 * config.x_scale);
    randomPlayback.setHeight(20 * config.y_scale);
    randomPlayback.setup(&config,1);
    randomPlayback.setFont(&config.f2());
    randomPlayback.setLabelString("Random Playback");

    //Editor
    ofAddListener(panSlider.clickedEvent, this, &ofApp::onSliderClicked);    
    ofAddListener(pitchSlider.clickedEvent, this, &ofApp::onSliderClicked);
    ofAddListener(gainSlider.clickedEvent, this, &ofApp::onSliderClicked);

    //Main
    ofAddListener(reverbSend.clickedEvent, this, &ofApp::onSliderClicked);
    ofAddListener(minDelay.numberChangedEvent, this, &ofApp::onNumberChanged);
    ofAddListener(maxDelay.numberChangedEvent, this, &ofApp::onNumberChanged);
    ofAddListener(randomPlayback.clickedEvent, this, &ofApp::onCheckboxClicked);

    maxScenes = 0;

    // Load global config data
    loadConfig();

    // create scenes
    if(maxScenes == 0) {
        maxScenes = 4;
        for(size_t i = 0; i < maxScenes; i++)
        {
            int x = config.baseSceneOffset;
            int y = i* config.scene_spacing + config.scene_yoffset;
            Scene* s = new Scene(&config,"",i,1,x,y,config.scene_width,config.scene_height);
            scenes.push_back(s);
        }
    }

    // setup Add Scene button
    addScene = new AddScene(&config,config.baseSceneOffset, scenes[maxScenes-1]->y + config.scene_spacing,config.scene_height,config.scene_height);
    addScene->setup();

    bLoading = true;
    bLoadingScenes = true;
}

//--------------------------------------------------------------
void ofApp::updateMainSliders()
{
    int min_d = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMinDelay();
    minDelay.setValue(min_d);

    int max_d = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMaxDelay();
    maxDelay.setValue(max_d);

    float send = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getReverbSend();
    reverbSend.setPercent(send);

    bool playRandom = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.isPlayingRandom();
    randomPlayback.isActive = playRandom;
}

//--------------------------------------------------------------
void ofApp::updateEditSliders()
{
    if(scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)) {
        float p = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)->getPan();
        float pct = (p+1.0f) / 2.0f;
        panSlider.setPercent(pct);

        float pitch = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)->getPitch();
        float total = pitchSlider.getHighValue() - pitchSlider.getLowValue();
        pct = (pitch - pitchSlider.getLowValue()) / total;
        //cout << "pitch: " << pitch << " total: " << total << " percent: " << pct << endl;
        pitchSlider.setPercent(pct);

        float gain = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)->getGain();
        total = gainSlider.getHighValue() - gainSlider.getLowValue();
        pct = (gain - gainSlider.getLowValue()) / total;
        //cout << "gain: " << gain << " total: " << total << " percent: " << pct << endl;
        gainSlider.setPercent(pct);
    }
}

//--------------------------------------------------------------
void ofApp::onObjectClicked(size_t& arg)
{
    ofLogNotice() << "sound object clicked: " << arg;
    updateMainSliders();
}

//--------------------------------------------------------------
void ofApp::onSampleClicked(int& arg)
{
    ofLogNotice() << "sample clicked: " << arg;
    config.activeSampleIdx = arg;
    updateEditSliders();    
}

//--------------------------------------------------------------
void  ofApp::onCheckboxClicked(Interactive::ClickArgs& args)
{
    if(randomPlayback.bActivate) {
        randomPlayback.bActivate = false;
        randomPlayback.isActive = !randomPlayback.isActive;
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setRandomPlayback(randomPlayback.isActive);
    }
}

//--------------------------------------------------------------
void ofApp::onNumberChanged(NumberBoxData& args)
{
    if(args.id == 1)
    {
        //min delay value
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setMinDelay(minDelay.getValue());
        int num = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size();
        for(int i = 0; i < num;i++)
        {
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.recalculateDelay(i);
        }
    }
    if(args.id == 2) {
        // max delay value
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setMaxDelay(maxDelay.getValue());
        int num = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size();
        for(int i = 0; i < num;i++)
        {
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.recalculateDelay(i);
        }
    }
}

//--------------------------------------------------------------
void ofApp::onSliderClicked(SliderData& args) {
    //ofLogNotice() << " clicked id: " << args.id << "  value: " << args.value;
    if( pageState == MAIN) {
        if(args.id == 4) {
            // reverb send
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setReverbSend(reverbSend.getValue());
        }
    }

    if(pageState == EDIT) {
        if(scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)) {
            if(args.id == 3) {
                // panning
                //scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setPan(pan.getValue());
                scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)->setPan(panSlider.getValue());
            }
            if(args.id == 5) {
                // pitch
                scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)->setPitch(pitchSlider.getValue());
                //cout << " pitch = " << pitchSlider.getValue() << endl;
            }
            if(args.id == 6) {
                // gain
                scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.at(config.activeSampleIdx)->setGain(gainSlider.getValue());
                //cout << " gain = " << gainSlider.getValue() << endl;
            }
        }
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    if(bLoading) return;

    if(pageState == MAIN) {
        // clear pad
        if(bClearPad) {
            int x = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->x;
            int y = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->y;
            int id = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->id;
            delete scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx];
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx] = new SoundObject(&config,config.activeScene,id,x,y,config.size,config.size);
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->setup();
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->enableAllEvents();
            bClearPad = false;
        }

        if(randomPlayback.bActivate) {
            randomPlayback.bActivate = false;
            randomPlayback.isActive = !randomPlayback.isActive;
        }
    }

    if(bLoadScenes) {
        ofFileDialogResult result = ofSystemLoadDialog("Load Feedra scenes", false);
        if(result.bSuccess) {
            loadConfig(result.filePath);
            // setup scenes
            for(size_t i=0;i < scenes.size();i++) {
                scenes[i]->setup(result.filePath);
            }
            config.activeSceneIdx = 0;
            enableScene(config.activeSceneIdx);
            updateMainSliders();

            for(size_t i=0;i < scenes.size();i++) {
                if(config.activeScene != scenes[i]->id) {
                    for(size_t j = 0; j < scenes[i]->sounds.size();j++)
                    {
                        scenes[i]->sounds[j]->disableAllEvents();
                    }
                }
            }
            updateScenePosition();
            addScene->enableEvents();
            bLoadScenes = false;

            calculateSources();
        }
    }

    checkAudioDeviceChange();

    for(size_t i=0; i < scenes.size();i++) {
         if(scenes[i]->selectScene) {
            cout << "found selected scene" << endl;
            scenes[i]->selectScene = false;
            enableScene(i);
         }
    }

    config.masterVolume = mainVolume.getValue();

    for(size_t i=0; i < scenes.size();i++) {
        scenes[i]->update();
    }

    //Add scene
    if(addScene->doAddScene) {
        addNewScene();
    }

    //Delete scene
    if(scenes[config.activeSceneIdx]->delete_scene.doDeleteScene)
    {
        deleteScene();
    }

}

//--------------------------------------------------------------
void ofApp::addNewScene()
{
    addScene->doAddScene = false;

    int x = config.baseSceneOffset;
    int y = scenes.size() * config.scene_spacing + config.scene_yoffset;
    int scene_width = config.scene_width;
    int scene_height = config.scene_height;

    // Sort scenes
    int new_id = 0;
    int cur_id = 0;
    int prev_id = 0;

    // sort by id:
    vector<Scene*> tmp_scenes;
    tmp_scenes = scenes;
    std::sort(tmp_scenes.begin(), tmp_scenes.end(),
        [](Scene* a, Scene* b) {
            return a->id < b->id;
        });

    for(unsigned int i = 0; i < tmp_scenes.size();i++)
    {
        cur_id = tmp_scenes[i]->id;
        if((cur_id - prev_id) > 1)
        {
            new_id = prev_id+1;
            break;
        }
        prev_id = cur_id;
        new_id = cur_id+1;
    }
    if(tmp_scenes[0]->id > 0) {
        new_id = 0;
    }

    Scene* s = new Scene(&config,"",new_id,1,x,y,scene_width,scene_height);
    s->setup("empty");
    scenes.push_back(s);

    int new_idx = 0;
    for(unsigned int i = 0; i < scenes.size();i++)
    {
        if(scenes[i]->id == new_id)
        {
            new_idx = i;
            break;
        }
    }

    addScene->y = y + config.scene_spacing;

    if(scenes.size() >= config.max_scenes) {
        addScene->disableEvents();
    }

    enableScene(new_idx);
}

//--------------------------------------------------------------
void ofApp::deleteScene()
{
    scenes[config.activeSceneIdx]->delete_scene.doDeleteScene = false;

    string scene_name = scenes[config.activeSceneIdx]->scene_name;
    string response = ofSystemTextBoxDialog("Are you sure you want to delete "+scene_name+"? Type 'yes' to delete", "no");
    //cout << "response  = " << response << endl;
    if((response.compare("yes") == 0) || (response.compare("YES") == 0))
    {
        if(scenes.size() == 1) {
            ofLogError() << "Cannot delete scene - must have a least one scene in project";
            return;
        }
        size_t scene_to_delete = config.activeSceneIdx;

        delete scenes[scene_to_delete];
        scenes.erase(scenes.begin()+scene_to_delete);

        enableScene(0);

        if(scenes.size() < config.max_scenes) {
            addScene->enableEvents();
        }

        updateScenePosition();
    }
}

//--------------------------------------------------------------
void ofApp::enableScene(int idx) {
cout << "start enable scene" << endl;
    for(unsigned int i =0; i < scenes.size();i++) {
        scenes[i]->endFade();
    }
    config.prevSceneIdx = config.activeSceneIdx;
    config.activeScene = scenes[idx]->id;
    for(unsigned int i =0; i < scenes.size();i++) {
        if(config.activeScene == scenes[i]->id) {
            config.activeSceneIdx = i;
        }
    }
    config.activeSoundIdx = scenes[idx]->activeSoundIdx;

//    for(size_t i=0; i < scenes.size();i++) {
//        for(size_t j = 0; j < scenes[i]->sounds.size();j++)
//        {
//            scenes[i]->sounds[j]->disableAllEvents();
//        }
//        scenes[i]->disable();
//    }
    for(size_t j = 0; j < scenes[config.prevSceneIdx]->sounds.size();j++)
    {
        scenes[config.prevSceneIdx]->sounds[j]->disableAllEvents();
    }
    scenes[config.prevSceneIdx]->disable();

    for(size_t j = 0; j < scenes[idx]->sounds.size();j++)
    {
        scenes[idx]->sounds[j]->enableAllEvents();
    }
    scenes[idx]->enable();
    cout << "Enable scene: " << idx << endl;
}

//--------------------------------------------------------------
void ofApp::disableEvents()
{
    for(size_t i=0; i < scenes.size();i++) {
        for(size_t j = 0; j < scenes[i]->sounds.size();j++)
        {
            scenes[i]->sounds[j]->disableAllEvents();
        }
        scenes[i]->disable();
        scenes[i]->disableInteractivity();
    }
    cout << "Disable Events" << endl;
}

//--------------------------------------------------------------
void ofApp::enableEditorMode()
{
    gainSlider.enableEvents();
    pitchSlider.enableEvents();
    randomPlayback.disableEvents();
    config.activeSampleIdx = 0;
    updateEditSliders();
    scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->enableEditorMode();

}

//--------------------------------------------------------------
void ofApp::disableEditorMode()
{
    gainSlider.disableEvents();
    pitchSlider.disableEvents();
    randomPlayback.enableEvents();
    scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->disableEditorMode();
}

//--------------------------------------------------------------
void ofApp::enableEvents()
{
    int idx = config.activeSceneIdx;
    for(size_t j = 0; j < scenes[idx]->sounds.size();j++)
    {
        scenes[idx]->sounds[j]->enableAllEvents();
    }
    scenes[idx]->enable();

    for(size_t i=0; i < scenes.size();i++) {
        scenes[i]->enableInteractivity();
    }

    cout << "Enable Events: " << idx << endl;
}

//--------------------------------------------------------------
void ofApp::updateScenePosition()
{
    for(int i=0;i < (int)scenes.size();i++) {
        int y = i* config.scene_spacing + config.scene_yoffset;
        scenes[i]->updatePosition(scenes[i]->x,y);
    }

    int y = scenes.size() * config.scene_spacing + config.scene_yoffset;
    addScene->y = y;
}

//--------------------------------------------------------------
void ofApp::draw(){

    if(bLoading) {
        ofSetWindowTitle("Loading");
        static unsigned int progress = 0;

        if(bLoadingScenes) {
            //Load everything else!
            scenes[progress]->setup();
            progress++;
            if(progress >= scenes.size())
            {
                bLoadingScenes = false;
            } else {
                ofPushStyle();
                ofSetColor(255);
                float barw = 200.0f;
                float barh = 40.0f;
                ofDrawRectangle(ofGetWidth()/2 - barw/2.0f, ofGetHeight()/2 - barh/2.0f, barw*config.x_scale,barh*config.y_scale);
                ofSetColor(0,0,255);
                ofDrawRectangle(ofGetWidth()/2 - barw/2.0f, ofGetHeight()/2 - barh/2.0f,(barw/scenes.size()*progress)*config.x_scale,barh*config.y_scale);
                ofPopStyle();
                return;
            }
        }

        ofLogNotice() << "Finished loading scenes, setting up Feedra";
        config.prevSceneIdx = 0;
        enableScene(config.activeSceneIdx);
        updateMainSliders();
        ofAddListener(SoundObject::clickedObjectEvent, this, &ofApp::onObjectClicked);
        ofAddListener(AudioSample::clickedSampleEvent, this, &ofApp::onSampleClicked);
        calculateSources();
        bLoading = false;
        return;
    }
    int fps = ofGetFrameRate();
    ofSetWindowTitle("Fps: "+ofToString(fps));

    static bool bEnableEvents = false;

    if(!bDoRender || bMinimised) return;

    ofBackgroundHex(0x9a8e84);

   if(pageState == EDIT) {
            if(scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size() > 0) {
                if(!bEnableEvents) {
                    enableEditorMode();
                    disableEvents();
                }
                renderEditPage();
                bEnableEvents = true;
            } else {
                // No samples on this pad, return to main
                pageState = MAIN;
            }
    }
   if(pageState == MAIN)
    {
        if(bEnableEvents) {
            enableEvents();
            disableEditorMode();
            bEnableEvents = false;
        }
        renderMainPage();
    }

// Test drawing of potential clickable Tab:
//    ofPushStyle();
//    ofSetLineWidth(2*config.x_scale);
//    ofNoFill();
//    //ofSetHexColor(0xFF9933);
//    ofSetHexColor(0x68533e);
//    ofTranslate(-150*config.x_scale,-20*config.y_scale,0);
//    ofBeginShape();

//    ofVertex(150*config.x_scale,700*config.y_scale);
//    float x0 = 200*config.x_scale;
//    float y0 = 700*config.y_scale;
//    float x1 = 210*config.x_scale;
//    float y1 = 690*config.y_scale;
//    float x2 = 210*config.x_scale;
//    float y2 = 680*config.y_scale;
//    float x3 = 220*config.x_scale;
//    float y3 = 670*config.y_scale;
//    ofVertex(x0,y0);
//    ofBezierVertex(x1,y1,x2,y2,x3,y3);

//    ofVertex(260*config.x_scale,670*config.y_scale);
//    ofVertex(300*config.x_scale,670*config.y_scale);

//     x0 = 310*config.x_scale;
//     y0 = 670*config.y_scale;
//     x1 = 320*config.x_scale;
//     y1 = 680*config.y_scale;
//     x2 = 320*config.x_scale;
//     y2 = 690*config.y_scale;
//     x3 = 330*config.x_scale;
//     y3 = 700*config.y_scale;
//    ofVertex(x0,y0);
//    ofBezierVertex(x1,y1,x2,y2,x3,y3);
//    ofVertex(380*config.x_scale,700*config.y_scale);

//    ofEndShape();

//    ofSetColor(64);
//    ofBeginShape();
//    ofVertex(150*config.x_scale,668*config.y_scale);
//    ofVertex(400*config.x_scale,668*config.y_scale);
//    ofEndShape();

//    ofSetColor(64);
//    ofBeginShape();
//    ofVertex(150*config.x_scale,700*config.y_scale);
//    ofVertex(400*config.x_scale,700*config.y_scale);
//    ofEndShape();
//ofPopStyle();

}

//--------------------------------------------------------------
void ofApp::renderMainPage()
{
    for(size_t i=0; i < scenes.size();i++) {
        scenes[i]->render();
    }

    if(scenes.size() < config.max_scenes) {
        addScene->render(0x7b2800);
    } else {
        //change addScene button if max scenes reached
        //addScene->render(0x404040);
    }

    // Current selected sound info
    if(scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.isLoaded()) {
        drawSoundInfo();
    }
    mainVolume.render();
    if(scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size() > 1)
    {
        randomPlayback.enableEvents();
        randomPlayback.render();
    } else {
        randomPlayback.disableEvents();
    }
}

//--------------------------------------------------------------
void ofApp::renderEditPage() {

    string name = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundname.text;
    config.f1().drawString(ofToUpper(name),30*config.x_scale, 30*config.y_scale);
    SoundPlayer& p = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer;
    for(size_t i=0; i < p.player.size();i++) {
        int offset = (i/15)*(config.sample_gui_width+20*config.x_scale);
        ofVec3f pos(config.xoffset+offset,config.yoffset+(i%15)*42*config.y_scale,0);
        p.player[i]->render(pos);
    }
    panSlider.render();
    pitchSlider.render();
    gainSlider.render();
}

//--------------------------------------------------------------
void ofApp::keyPressed  (ofKeyEventArgs & args){
    int key = args.key;
    //cout << "key = " << key <<  " cntrl is on? " << (int)args.hasModifier(OF_KEY_CONTROL) << endl;

    if((args.keycode == GLFW_KEY_1) && args.hasModifier(OF_KEY_CONTROL)){
        pageState = MAIN;
    } else
    if((args.keycode == GLFW_KEY_2) && args.hasModifier(OF_KEY_CONTROL)){
        pageState = EDIT;
    }
    if((args.keycode == GLFW_KEY_3) && args.hasModifier(OF_KEY_CONTROL)){
        pageState = SETTINGS;
    }
    if((args.keycode == GLFW_KEY_Q) && args.hasModifier(OF_KEY_CONTROL)){
        disableEvents();
        ofExit();
    }
    if((args.keycode == GLFW_KEY_D) && args.hasModifier(OF_KEY_CONTROL)){
        bClearPad = true;
    }
    if((args.keycode == GLFW_KEY_S) && args.hasModifier(OF_KEY_CONTROL)){
        ofFileDialogResult result = ofSystemSaveDialog("settings.json", "Save Feedra scenes");
        if(result.bSuccess) {
            saveConfig(result.filePath,true);
        }
    }
    if((args.keycode == GLFW_KEY_O) && args.hasModifier(OF_KEY_CONTROL)){
        bLoadScenes = true;
    }
    if(key == ' ' && args.hasModifier(OF_KEY_CONTROL)) {
        bDoRender = !bDoRender;
    }

    if(pageState == MAIN) {
        if(key == OF_KEY_UP)
        {
            if(config.activeSceneIdx > 0) {
                std::swap(scenes[config.activeSceneIdx],scenes[config.activeSceneIdx-1]);
                config.activeSceneIdx--;
                updateScenePosition();
            }
        }
        if(key == OF_KEY_DOWN)
        {
            if(config.activeSceneIdx < scenes.size()-1) {
                std::swap(scenes[config.activeSceneIdx],scenes[config.activeSceneIdx+1]);
                config.activeSceneIdx++;
                updateScenePosition();
            }
        }
    }

    if(pageState == EDIT) {
        if(key == OF_KEY_UP)
        {
            if(config.activeSampleIdx > 0) {
                SoundObject* curSoundObj = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx];
                std::swap(curSoundObj->soundPlayer.player[config.activeSampleIdx], curSoundObj->soundPlayer.player[config.activeSampleIdx-1]);
               config.activeSampleIdx--;
               cout << "config.activeSampleIdx = " << config.activeSampleIdx << endl;
               updateEditSliders();
            }
        }
        if(key == OF_KEY_DOWN)
        {
            SoundObject* curSoundObj = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx];
            if(config.activeSampleIdx < curSoundObj->soundPlayer.player.size()-1) {
               std::swap(curSoundObj->soundPlayer.player[config.activeSampleIdx],curSoundObj->soundPlayer.player[config.activeSampleIdx+1]);
               config.activeSampleIdx++;
               cout << "config.activeSampleIdx = " << config.activeSampleIdx << endl;
               updateEditSliders();
            }
        }
    }
}

//--------------------------------------------------------------
void ofApp::checkAudioDeviceChange()
{
    curAppTime = ofGetElapsedTimeMillis();
    if((curAppTime - prevAppTime) > 1000)
    {
        //Need to list devices first
        int num_devices = OpenALSoundPlayer::listDevices(false);
        newDevice = OpenALSoundPlayer::getDefaultDevice();
        prevAppTime = curAppTime;

        if(newDevice.compare(curDevice) != 0) {
            OpenALSoundPlayer::reopenDevice(newDevice.c_str());
            ofLogNotice() << "New default audio device opened: " << newDevice << " Num devices = " << num_devices;
        }
        curDevice = newDevice;
    }
}

void ofApp::drawSoundInfo()
{
    int mind = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMinDelay();
    int maxd = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMaxDelay();
    float total = (float) scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getTotalDelay();

    std::stringstream stream;
    //stream << std::fixed << std::setprecision() << round(total);
    stream << std::fixed << std::setprecision(2) << total;
    std::string t = stream.str();

    int curSound = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getCurSound();
    string name = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundname.text;
    string path = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundpath[curSound];
    int chan = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->channels;
    int sr = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->sample_rate;
    string fs = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0]->audioPlayer->getFormatString();
    string fsub = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0]->audioPlayer->getSubFormatString();
    size_t numSounds = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size();

    ofSetColor(0);
    config.f2().drawString("Random delay: "+ofToString(t)+" secs", 250*config.x_scale, ofGetHeight() - 85*config.y_scale);
    config.f2().drawString("Num sounds: "+ofToString(numSounds), config.xoffset+600*config.x_scale, ofGetHeight() - 85*config.y_scale);

    ofSetColor(255);
    config.f2().drawString("channels: "+ofToString(chan), config.xoffset, ofGetHeight() - 95*config.y_scale);
    config.f2().drawString("format: "+ofToString(fs), config.xoffset, ofGetHeight() - 75*config.y_scale);
    config.f2().drawString("sub-format: "+ofToString(fsub), config.xoffset, ofGetHeight() - 55*config.y_scale);
    config.f2().drawString("sample rate: "+ofToString(sr), config.xoffset, ofGetHeight() - 35*config.y_scale);
    config.f2().drawString("path: "+path, config.xoffset, ofGetHeight() - 15*config.y_scale);


    minDelay.render();
    maxDelay.render();
    reverbSend.render();
}

//--------------------------------------------------------------
void ofApp::stopAllSounds()
{
    for(size_t i=0; i < scenes.size();i++) {
        scenes[i]->stop();
    }
}

//--------------------------------------------------------------
void ofApp::keyReleased(ofKeyEventArgs & args){
    int key = args.key;

    if((key == 'd' || key == 'D') && args.hasModifier(OF_KEY_CONTROL)){
        bClearPad = false;
    }
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){


}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
    //cout << "drag info  position: " << dragInfo.position << " num files: " << dragInfo.files.size() << endl;
}
