#include "ofApp.h"
#include "GLFW/glfw3.h"

#include "AL/alext.h"

bool ofApp::bMinimised = false;

//--------------------------------------------------------------
ofApp::~ofApp()
{    
    ofRemoveListener(minDelay.clickedEvent, this, &ofApp::onClicked);
    ofRemoveListener(maxDelay.clickedEvent, this, &ofApp::onClicked);
    ofRemoveListener(pan.clickedEvent, this, &ofApp::onClicked);
    ofRemoveListener(reverbSend.clickedEvent, this, &ofApp::onClicked);

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
    saveConfig("settings/settings.json", false);
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

        //save sounds
        for(int j=0;j < scenes[i]->sounds.size();j++)
        {            
            if(bCopyFiles) {
                for(int k = 0; k < scenes[i]->sounds[j]->soundpath.size();k++) {
                    string oldpath = scenes[i]->sounds[j]->soundpath[k];
                    filesystem::path p(oldpath);
                    string name = p.filename();
    #ifdef TARGET_LINUX
                    filesystem::path np(newpath);
                    //string outfile = np.filename();
                    string rootdir = np.parent_path();
                    ofSystem("mkdir -p "+rootdir+"/files/");
                    string outpath = rootdir+"/files/"+name;
                    string command;
                    if(scenes[i]->sounds[j]->soundpath[k] == outpath) {
                        break;
                    }
                    command = "cp -f \""+scenes[i]->sounds[j]->soundpath[k]+"\" \""+outpath + "\"";
                    //cout<< "command = " << command << endl;

    #endif
                    ofSystem(command);
                    scenes[i]->sounds[j]->soundpath[k] = outpath;
                }
                scenes[i]->sounds[j]->save();
            } else {
                scenes[i]->sounds[j]->save();
            }

        }
    }
    //config.settings.push_back(sceneInfo);
    config.settings.merge_patch(sceneInfo);

    filesystem::path p(newpath);
    if(p.extension() != ".json") {
        newpath = (string) p.parent_path() + "/" + (string) p.stem() + ".json";
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
            int num = scenes[i]->sounds[j]->soundPlayer.player.size();
            for(int k=0;k < num;k++) {
                OpenALSoundPlayer* p = scenes[i]->sounds[j]->soundPlayer.player[k].audioPlayer;
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
    bDoRender = true;
    bLoadScenes = false;
    bClearPad = false;
    doClearPad = -1;
    pageState = PageState::MAIN;

    config.setup();

    mainVolume.setup(-1,config.xoffset,20*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,1,false,false);
    mainVolume.setScale(config.y_scale, config.x_scale);
    mainVolume.setFont(&config.f2());
    mainVolume.setLabelString("Main Volume");

    minDelay.setup(1,270*config.x_scale,ofGetHeight() - 120*config.y_scale,200*config.x_scale,20*config.y_scale,0,60,0,false,false);
    minDelay.setScale(config.y_scale, config.x_scale);
    minDelay.setFont(&config.f2());
    minDelay.setNumberDisplayPrecision(0);
    minDelay.setLabelString("Min delay");

    maxDelay.setup(2,270*config.x_scale,ofGetHeight() - 80*config.y_scale,200*config.x_scale,20*config.y_scale,0,120,0,false,false);
    maxDelay.setScale(config.y_scale, config.x_scale);
    maxDelay.setFont(&config.f2());
    maxDelay.setNumberDisplayPrecision(0);
    maxDelay.setLabelString("Max delay");

    pan.setup(3,config.xoffset+600*config.x_scale,ofGetHeight() - 120*config.y_scale,200*config.x_scale,20*config.y_scale,-1,1,0,false,false);
    pan.setScale(config.y_scale, config.x_scale);
    pan.setFont(&config.f2());
    pan.setLabelString("Panning");

    reverbSend.setup(4,config.xoffset+600*config.x_scale,ofGetHeight() - 80*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,0,false,false);
    reverbSend.setScale(config.y_scale, config.x_scale);
    reverbSend.setFont(&config.f2());
    reverbSend.setLabelString("Reverb send");

    setStereo.setX(config.xoffset);
    setStereo.setY(ofGetHeight() - 140*config.y_scale);
    setStereo.setWidth(20 * config.x_scale);
    setStereo.setHeight(20 * config.y_scale);
    setStereo.setup(&config);

    ofAddListener(minDelay.clickedEvent, this, &ofApp::onClicked);
    ofAddListener(maxDelay.clickedEvent, this, &ofApp::onClicked);
    ofAddListener(pan.clickedEvent, this, &ofApp::onClicked);
    ofAddListener(reverbSend.clickedEvent, this, &ofApp::onClicked);

    maxScenes = 0;

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

    // setup scenes
    for(size_t i=0;i < scenes.size();i++) {
        scenes[i]->setup();
    }

    enableScene(config.activeSceneIdx);

    updateSliders();

    for(size_t i=0;i < scenes.size();i++) {
        if(config.activeScene != scenes[i]->id) {
            for(size_t j = 0; j < scenes[i]->sounds.size();j++)
            {
                scenes[i]->sounds[j]->disableAllEvents();
            }
        }
    }

    ofAddListener(SoundObject::clickedObjectEvent, this, &ofApp::onObjectClicked);

    // Set the current default audio device
    curDevice = newDevice = OpenALSoundPlayer::getDefaultDevice();

    calculateSources();
}

//--------------------------------------------------------------
void ofApp::updateSliders()
{
    int md = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMinDelay();
    //cout << "md = " << md << endl;
    int l = minDelay.getHighValue();
    float pct = (float) md /(float) 1000 * 1.0f/l;
    //cout << "pct = " << pct << endl;
    minDelay.setPercent(pct);

    maxDelay.setLowValue(minDelay.getValue());
    maxDelay.setHighValue(minDelay.getValue()+30);
    md = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMaxDelay();
    //cout << "md = " << md << endl;
    l = minDelay.getHighValue();
    pct = (float) md /(float) 1000 * 1.0f/l;
    //cout << "pct = " << pct << endl;
    maxDelay.setPercent(pct);

    float p = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getPan();
    pct = (p+1.0f) / 2.0f;
    pan.setPercent(pct);

    float send = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getReverbSend();
    reverbSend.setPercent(send);
}

//--------------------------------------------------------------
void ofApp::onObjectClicked(size_t& args)
{
    //cout << "object clicked: " << args << endl;

    updateSliders();

    doClearPad = args;

}

//--------------------------------------------------------------
void ofApp::onClicked(SliderData& args) {
    //ofLogNotice() << " clicked id: " << args.id << "  value: " << args.value;

    if(args.id == 1) {
        //min value
        maxDelay.setLowValue(minDelay.getValue());
        maxDelay.setHighValue(minDelay.getValue()+30);
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setMinDelay(minDelay.getValue()*1000);
        int num = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size();
        for(int i = 0; i < num;i++)
        {
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.recalculateDelay(i);
        }
    }
    if(args.id == 2) {
        // max value
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setMaxDelay(maxDelay.getValue()*1000);
        int num = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player.size();
        for(int i = 0; i < num;i++)
        {
            scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.recalculateDelay(i);
        }
    }

    if(args.id == 3) {
        // panning
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setPan(pan.getValue());
    }
    if(args.id == 4) {
        // reverb send
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.setReverbSend(reverbSend.getValue());
    }

}

//--------------------------------------------------------------
void ofApp::update(){
    // clear pad
    if(bClearPad) {
        if(doClearPad >= 0) {
            int x = scenes[config.activeSceneIdx]->sounds[doClearPad]->x;
            int y = scenes[config.activeSceneIdx]->sounds[doClearPad]->y;
            int id = scenes[config.activeSceneIdx]->sounds[doClearPad]->id;
            delete scenes[config.activeSceneIdx]->sounds[doClearPad];
            scenes[config.activeSceneIdx]->sounds[doClearPad] = new SoundObject(&config,config.activeScene,id,x,y,config.size,config.size);
            scenes[config.activeSceneIdx]->sounds[doClearPad]->setup();
            bClearPad = false;
        }
    }
    doClearPad = -1;

    if(setStereo.bActivate) {
        setStereo.bActivate = false;
        setStereo.isActive = !setStereo.isActive;
    }

    if(bLoadScenes) {
        ofFileDialogResult result = ofSystemLoadDialog("Load Feedra scenes", false);
        loadConfig(result.filePath);
        // setup scenes
        for(size_t i=0;i < scenes.size();i++) {
            scenes[i]->setup(result.filePath);
        }
        enableScene(config.activeSceneIdx);
        updateSliders();

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

    checkAudioDeviceChange();

    for(size_t i=0; i < scenes.size();i++) {
         if(scenes[i]->selectScene) {
            scenes[i]->selectScene = false;
            enableScene(i);
         }
    }

    config.masterVolume = mainVolume.getValue();

    scenes[config.activeSceneIdx]->update();

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

    int baseSceneOffset = config.gridWidth*config.spacing ;
    int x = baseSceneOffset;
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

    for(int i = 0; i < tmp_scenes.size();i++)
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

    addScene->y = y + config.scene_spacing;

    if(scenes.size() >= config.max_scenes) {
        addScene->disableEvents();
    }

    enableScene(new_id);
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

    for(int i =0; i < scenes.size();i++) {
        scenes[i]->endFade();
    }

    config.activeScene = scenes[idx]->id;

    for(int i =0; i < scenes.size();i++) {
        if(config.activeScene == scenes[i]->id) {
            config.activeSceneIdx = i;
        }
    }

    config.activeSoundIdx = scenes[idx]->activeSoundIdx;

    for(size_t i=0; i < scenes.size();i++) {
        for(size_t j = 0; j < scenes[i]->sounds.size();j++)
        {
            scenes[i]->sounds[j]->disableAllEvents();
        }
        scenes[i]->disable();
    }

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
    for(size_t i=0;i < scenes.size();i++) {
        int y = i* config.scene_spacing + config.scene_yoffset;
        scenes[i]->updatePosition(scenes[i]->x,y);
    }

    int y = scenes.size() * config.scene_spacing + config.scene_yoffset;
    addScene->y = y;
}

//--------------------------------------------------------------
void ofApp::draw(){
    int fps = ofGetFrameRate();
    ofSetWindowTitle("Fps: "+ofToString(fps));

    static bool bEnableEvents = false;

    if(!bDoRender || bMinimised) return;

    ofBackgroundHex(0x9a8e84);

    if(pageState == MAIN)
    {
        if(bEnableEvents) {
            enableEvents();
            bEnableEvents = false;
        }

        renderMainPage();

    } else if(pageState == EDIT) {
        if(!bEnableEvents) {
            disableEvents();
        }
        renderEditPage();
        bEnableEvents = true;
    }

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


    setStereo.render();

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
}

//--------------------------------------------------------------
void ofApp::renderEditPage() {

    string name = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundname.text;
    config.f1().drawString(ofToUpper(name),30*config.x_scale, 30*config.y_scale);
    SoundPlayer& p = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer;
    for(size_t i=0; i < p.player.size();i++) {
        int offset = (i/15)*(config.sample_gui_width+20*config.x_scale);
        ofVec3f pos(config.xoffset+offset,config.yoffset+(i%15)*42*config.y_scale,0);
        p.player[i].render(pos);
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed  (ofKeyEventArgs & args){
    int key = args.key;

    if(key == '1'){
        pageState = MAIN;
    } else
    if(key == '2'){
        pageState = EDIT;
    }
    if(key == '3'){
        pageState = SETTINGS;
    }
    if((key == 'q' || key == 'Q') && args.hasModifier(OF_KEY_CONTROL)){
        ofExit();
    }
    if((key == 'd' || key == 'D') && args.hasModifier(OF_KEY_CONTROL)){
        bClearPad = true;
    }
    if((key == 's' || key == 'S') && args.hasModifier(OF_KEY_CONTROL)){
        ofFileDialogResult result = ofSystemSaveDialog("settings.json", "Save Feedra scenes");
        if(result.bSuccess) {
            saveConfig(result.filePath,true);
        }
    }
    if((key == 'o' || key == 'O') && args.hasModifier(OF_KEY_CONTROL)){
        bLoadScenes = true;
    }
    if(key == ' ' && args.hasModifier(OF_KEY_CONTROL)) {
        bDoRender = !bDoRender;
    }

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

//--------------------------------------------------------------
void ofApp::checkAudioDeviceChange()
{
    curTime = ofGetElapsedTimeMillis();
    if((curTime - prevTime) > 1000)
    {
        //Need to list devices first
        int num_devices = OpenALSoundPlayer::listDevices(false);
        newDevice = OpenALSoundPlayer::getDefaultDevice();
        prevTime = curTime;

        if(newDevice.compare(curDevice) != 0) {
            OpenALSoundPlayer::reopenDevice(newDevice.c_str());
            ofLogNotice() << "New default audio device opened: " << newDevice << " Num devices = " << num_devices;
        }
        curDevice = newDevice;
    }
}

void ofApp::drawSoundInfo()
{
    //cout << "config.activeSceneIdx = " << config.activeSceneIdx << " config.activeSoundIdx = " << config.activeSoundIdx << endl;
    int mind = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMinDelay();
    int maxd = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getMaxDelay();
    float total = (float) scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getTotalDelay()/1000.0f;

    std::stringstream stream;
    stream << std::fixed << std::setprecision(0) << round(total);
    std::string t = stream.str();

    int curSound = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.getCurSound();
    string name = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundname.text;
    string path = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundpath[curSound];
    int chan = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->channels;
    int sr = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->sample_rate;
    string fs = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].audioPlayer->getFormatString();
    string fsub = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].audioPlayer->getSubFormatString();

    ofSetColor(0);
    config.f2().drawString("Random delay: "+ofToString(t)+" secs", 268*config.x_scale, ofGetHeight() - 45*config.y_scale);
    //config.f2().drawString("min delay: "+ofToString(mind), 270*config.x_scale, ofGetHeight() - 50*config.y_scale);
    //config.f2().drawString("max delay: "+ofToString(maxd), 270*config.x_scale, ofGetHeight() - 30*config.y_scale);
    ofSetColor(255);
    //config.f2().drawString("name: "+name, 400*config.x_scale, ofGetHeight() - 50*config.y_scale);
    config.f2().drawString("channels: "+ofToString(chan), config.xoffset, ofGetHeight() - 95*config.y_scale);
    config.f2().drawString("format: "+ofToString(fs), config.xoffset, ofGetHeight() - 75*config.y_scale);
    config.f2().drawString("sub-format: "+ofToString(fsub), config.xoffset, ofGetHeight() - 55*config.y_scale);
    config.f2().drawString("sample rate: "+ofToString(sr), config.xoffset, ofGetHeight() - 35*config.y_scale);
    config.f2().drawString("path: "+path, config.xoffset, ofGetHeight() - 15*config.y_scale);


    minDelay.render();
    maxDelay.render();
    pan.render();
    reverbSend.render();
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
