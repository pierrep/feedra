#include "ofApp.h"
#include "GLFW/glfw3.h"

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
    saveConfig("settings/settings.json");
}

//--------------------------------------------------------------
void ofApp::saveConfig(string path)
{
    config.settings.clear();

    ofJson globalsettings;    
    globalsettings["mainvolume"] = mainVolume.getValue();
    globalsettings["maxscenes"] = scenes.size();
    config.settings.push_back(globalsettings);

    for(size_t i=0;i < scenes.size();i++) {
        ofJson sceneInfo;
        sceneInfo["scene"+ofToString(i)] = scenes[i]->id;
        sceneInfo["scene"+ofToString(i)+"-name"] = scenes[i]->textfield.text;
        sceneInfo["scene"+ofToString(i)+"-activesound"] = scenes[i]->activeSound;
        config.settings.push_back(sceneInfo);

        for(int j=0;j < scenes[i]->sounds.size();j++)
        {
            scenes[i]->sounds[j]->save();
        }
    }
    string save_name = path;
    ofSavePrettyJson(save_name,config.settings);
}

//--------------------------------------------------------------
void ofApp::loadScenes()
{
    ofJson json;
    ofFile file("settings/settings.json");

    if(file.exists()){
        file >> json;
        for(auto & setting: json){
            if(!setting.empty())
            {
                if(!setting["maxscenes"].empty())
                {
                    maxScenes = setting["maxscenes"];
                }
                if(!setting["mainvolume"].empty())
                {
                    float v = setting["mainvolume"];
                    mainVolume.setPercent(v);
                }
            }
        }

    if(maxScenes > 0) {
        for(auto & setting: json){
            if(!setting.empty())
            {
                for(int i=0; i < maxScenes;i++) {
                    if(!setting["scene"+ofToString(i)].empty())
                    {
                        int new_scene_id = setting["scene"+ofToString(i)];
                        //cout << "new_scene_id: " << new_scene_id << endl;

                        int x = config.baseSceneOffset;
                        int y = i* config.scene_spacing + config.scene_yoffset;
                        string name = "";
                        if(!setting["scene"+ofToString(i)+"-name"].empty())
                        {
                            name = setting["scene"+ofToString(i)+"-name"];
                        }
                        int asid = new_scene_id*100 +1;
                        if(!setting["scene"+ofToString(i)+"-activesound"].empty())
                        {
                            asid = setting["scene"+ofToString(i)+"-activesound"];
                        }
                        Scene* s = new Scene(&config,name,new_scene_id,asid,x,y,config.scene_width,config.scene_height);
                        scenes.push_back(s);
                    }
                }
            }
        }
    }

    }
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

        ofSetWindowShape(ofGetWindowWidth()*config.x_scale,ofGetWindowHeight()*config.y_scale);
    }
    bDoRender = true;

    config.setup();

    mainVolume.setup(-1,config.xoffset,20*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,1,false,false);
    mainVolume.setScale(config.y_scale, config.x_scale);
    mainVolume.setFont(&config.f2());
    mainVolume.setLabelString("Main Volume");

    minDelay.setup(1,config.xoffset,ofGetHeight() - 80*config.y_scale,200*config.x_scale,20*config.y_scale,0,60,0,false,false);
    minDelay.setScale(config.y_scale, config.x_scale);
    minDelay.setFont(&config.f2());
    minDelay.setNumberDisplayPrecision(0);
    minDelay.setLabelString("Min delay");

    maxDelay.setup(2,config.xoffset,ofGetHeight() - 40*config.y_scale,200*config.x_scale,20*config.y_scale,0,120,0,false,false);
    maxDelay.setScale(config.y_scale, config.x_scale);
    maxDelay.setFont(&config.f2());
    maxDelay.setNumberDisplayPrecision(0);
    maxDelay.setLabelString("Max delay");

    pan.setup(3,config.xoffset+600*config.x_scale,ofGetHeight() - 80*config.y_scale,200*config.x_scale,20*config.y_scale,-1,1,0,false,false);
    pan.setScale(config.y_scale, config.x_scale);
    pan.setFont(&config.f2());
    pan.setLabelString("Panning");

    reverbSend.setup(4,config.xoffset+600*config.x_scale,ofGetHeight() - 55*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,0,false,false);
    reverbSend.setScale(config.y_scale, config.x_scale);
    reverbSend.setFont(&config.f2());
    reverbSend.setLabelString("Reverb send");

    ofAddListener(minDelay.clickedEvent, this, &ofApp::onClicked);
    ofAddListener(maxDelay.clickedEvent, this, &ofApp::onClicked);
    ofAddListener(pan.clickedEvent, this, &ofApp::onClicked);
    ofAddListener(reverbSend.clickedEvent, this, &ofApp::onClicked);

    maxScenes = 0;
    loadScenes();

    // create scenes    
    if(maxScenes == 0) {
        maxScenes = 4;
        for(size_t i = 0; i < maxScenes; i++)
        {
            int x = config.baseSceneOffset;
            int y = i* config.scene_spacing + config.scene_yoffset;
            Scene* s = new Scene(&config,"",i,i*100+1,x,y,config.scene_width,config.scene_height);
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
//    config.activeScene = scenes[0]->id;
//    config.activeSound = scenes[0]->activeSound;
//    int idx = config.activeSound - config.activeSceneIdx*100 -1;
//    config.activeSoundIdx =  idx;
    enableScene(0);

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
void ofApp::onObjectClicked(int& args)
{
    //cout << "object clicked: " << args << endl;

    updateSliders();

}

//--------------------------------------------------------------
void ofApp::onClicked(SliderData& args) {
    //ofLogNotice() << " clicked id: " << args.id << "  value: " << args.value;

    if(args.id == 1) {
        //min value
        maxDelay.setLowValue(minDelay.getValue());
        maxDelay.setHighValue(minDelay.getValue()+30);
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].minDelay = minDelay.getValue()*1000;
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.recalculateDelay();
    }
    if(args.id == 2) {
        // max value
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].maxDelay = maxDelay.getValue()*1000;
        scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.recalculateDelay();
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

    int baseSceneOffset = config.gridWidth*config.spacing + config.xoffset ;
    int x = baseSceneOffset;
    int y = scenes.size() * config.scene_spacing + config.scene_yoffset;
    int scene_width = 200*config.x_scale;
    int scene_height = 40*config.y_scale;

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

    Scene* s = new Scene(&config,"",new_id,new_id*100+1,x,y,scene_width,scene_height);
    s->setup();
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
    cout << "response  = " << response << endl;
    if((response.compare("yes") == 0) || (response.compare("YES") == 0))
    {
        size_t scene_to_delete = config.activeSceneIdx;

        for(size_t j = 0; j < scenes[scene_to_delete]->sounds.size();j++)
        {
            scenes[scene_to_delete]->sounds[j]->isSetup = false;
        }
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
void ofApp::enableScene(int i) {
    config.activeScene = scenes[i]->id;

    for(int i =0; i < scenes.size();i++) {
        if(config.activeScene == scenes[i]->id) {
            config.activeSceneIdx = i;
        }
    }

    config.activeSound = scenes[i]->activeSound;
    int idx = config.activeSound - config.activeSceneIdx*100 -1;
    config.activeSoundIdx =  idx;

    for(size_t i=0; i < scenes.size();i++) {
        for(size_t j = 0; j < scenes[i]->sounds.size();j++)
        {
            scenes[i]->sounds[j]->disableAllEvents();
        }
        scenes[i]->disable();
    }

    for(size_t j = 0; j < scenes[i]->sounds.size();j++)
    {
        scenes[i]->sounds[j]->enableAllEvents();
    }
    scenes[i]->enable();
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
    if(!bDoRender) return;

    ofBackgroundHex(0x9a8e84);

    for(size_t i=0; i < scenes.size();i++) {
         scenes[i]->render();
     }

    if(scenes.size() < config.max_scenes) {
        addScene->render(0x7b2800);
    } else {
        addScene->render(0x404040);
    }

    // Current selected sound info
    if(scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.isLoaded()) {
        //cout << "config.activeSceneIdx = " << config.activeSceneIdx << " config.activeSoundIdx = " << config.activeSoundIdx << endl;
        int mind = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].minDelay;
        int maxd = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].maxDelay;
        float total = (float) scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].totalDelay/1000.0f;

        std::stringstream stream;
        stream << std::fixed << std::setprecision(0) << round(total);
        std::string t = stream.str();

        string name = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundname.text;
        string path = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundpath;
        int chan = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->channels;
        int sr = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->sample_rate;
        string fs = scenes[config.activeSceneIdx]->sounds[config.activeSoundIdx]->soundPlayer.player[0].audioPlayer->getFormatString();

        ofSetColor(0);
        config.f1().drawString("Random delay:\n\n"+ofToString(t)+" secs", 270*config.x_scale, ofGetHeight() - 70*config.y_scale);
        //config.f2().drawString("min delay: "+ofToString(mind), 270*config.x_scale, ofGetHeight() - 50*config.y_scale);
        //config.f2().drawString("max delay: "+ofToString(maxd), 270*config.x_scale, ofGetHeight() - 30*config.y_scale);
        ofSetColor(255);
        config.f2().drawString("name: "+name, 400*config.x_scale, ofGetHeight() - 50*config.y_scale);
        config.f2().drawString("path: "+path, 400*config.x_scale, ofGetHeight() - 30*config.y_scale);
        config.f2().drawString("channels: "+ofToString(chan), 400*config.x_scale, ofGetHeight() - 70*config.y_scale);
        config.f2().drawString("sample rate: "+ofToString(sr), 400*config.x_scale, ofGetHeight() - 10*config.y_scale);
        config.f2().drawString("type: "+ofToString(fs), 600*config.x_scale, ofGetHeight() - 10*config.y_scale);

        minDelay.render();
        maxDelay.render();
        pan.render();
        reverbSend.render();
    }
    mainVolume.render();
}

//--------------------------------------------------------------
void ofApp::keyPressed  (ofKeyEventArgs & args){
    int key = args.key;

    if((key == 'q' || key == 'Q') && args.hasModifier(OF_KEY_CONTROL)){
        ofExit();
    }
    if((key == 's' || key == 'S') && args.hasModifier(OF_KEY_CONTROL)){
        ofFileDialogResult result = ofSystemSaveDialog("settings.json", "Save Feedra app settings");        
        saveConfig(result.filePath);
    }
    if(key == ' ' && args.hasModifier(OF_KEY_CONTROL)) {
        bDoRender = !bDoRender;
    }

    if(key == OF_KEY_UP)
    {
        if(config.activeSceneIdx > 0) {
            std::swap(scenes[config.activeSceneIdx],scenes[config.activeSceneIdx-1]);
            updateScenePosition();
        }
    }
    if(key == OF_KEY_DOWN)
    {
        if(config.activeSceneIdx < scenes.size()-1) {
            std::swap(scenes[config.activeSceneIdx],scenes[config.activeSceneIdx+1]);
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
        int num_devices = OpenALSoundPlayer::listDevices(false);
        newDevice = OpenALSoundPlayer::getDefaultDevice();
        //ofLogNotice() << "Num devices = " << num_devices << "  default device: " << newDevice;
        prevTime = curTime;

        if(newDevice.compare(curDevice) != 0) {
            OpenALSoundPlayer::reopenDevice(newDevice.c_str());
        }
        curDevice = newDevice;
    }
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

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
    cout << "drag info  position: " << dragInfo.position << " num files: " << dragInfo.files.size() << endl;
}
