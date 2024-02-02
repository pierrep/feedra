#include "ofApp.h"
#include "GLFW/glfw3.h"

ofApp::~ofApp()
{    
    ofJson mainvol;
    mainvol["mainvolume"] = mainVolume.getValue();
    cout << mainVolume.getValue() << endl;
    config.settings.push_back(mainvol);

    ofJson sceneId;
    sceneId["maxscenes"] = scenes.size();
    config.settings.push_back(sceneId);

    for(size_t i=0;i < scenes.size();i++) {
        for(size_t j = 0; j < scenes[i]->sounds.size();j++)
        {
            scenes[i]->sounds[j]->disableAllEvents();
        }
        scenes[i]->disable();

        ofJson sceneInfo;
        sceneInfo["scene"+ofToString(i)] = scenes[i]->id;
        sceneInfo["scene"+ofToString(i)+"-name"] = scenes[i]->textfield.text;
        config.settings.push_back(sceneInfo);
    }
    for(size_t i=0;i < scenes.size();i++) {        
        delete scenes[i];
    }
    scenes.clear();

    ofSaveJson("settings/settings.json",config.settings);

    delete addScene;
}

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
                        int y = i* config.scene_spacing + config.yoffset;
                        string name = "";
                        if(!setting["scene"+ofToString(i)+"-name"].empty())
                        {
                            name = setting["scene"+ofToString(i)+"-name"];
                        }
                        Scene* s = new Scene(&config,name,new_scene_id,x,y,config.scene_width,config.scene_height);
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

    maxScenes = 0;
    loadScenes();

    // create scenes    
    if(maxScenes == 0) {
        maxScenes = 4;
        for(size_t i = 0; i < maxScenes; i++)
        {
            int x = config.baseSceneOffset;
            int y = i* config.scene_spacing + config.yoffset;
            Scene* s = new Scene(&config,"",i,x,y,config.scene_width,config.scene_height);
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
    config.activeScene = scenes[0]->id;
    for(size_t i=0;i < scenes.size();i++) {
        if(config.activeScene != scenes[i]->id) {
            for(size_t j = 0; j < scenes[i]->sounds.size();j++)
            {
                scenes[i]->sounds[j]->disableAllEvents();
            }
        }
    }

}

//--------------------------------------------------------------
void ofApp::enableScene(int i) {

    for(size_t k=0; k < scenes.size();k++) {
        for(size_t j = 0; j < scenes[i]->sounds.size();j++)
        {
            scenes[k]->sounds[j]->disableAllEvents();
        }
        scenes[k]->disable();
    }

    for(size_t j = 0; j < scenes[i]->sounds.size();j++)
    {
        scenes[i]->sounds[j]->enableAllEvents();
    }
    scenes[i]->enable();
}

//--------------------------------------------------------------
void ofApp::update(){
    for(int i =0; i < scenes.size();i++) {
        if(config.activeScene == scenes[i]->id) {
            config.activeSceneIdx = i;
        }
    }

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
        int baseSceneOffset = config.gridWidth*config.spacing + config.xoffset ;
        int x = baseSceneOffset;
        int y = scenes.size() * config.scene_spacing + config.yoffset;
        int scene_width = 200*config.x_scale;
        int scene_height = 40*config.y_scale;

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

        Scene* s = new Scene(&config,"",new_id,x,y,scene_width,scene_height);
        s->setup();
        scenes.push_back(s);

        for(size_t i=0;i < scenes.size();i++) {
            if(config.activeScene != scenes[i]->id) {
                for(size_t j = 0; j < scenes[i]->sounds.size();j++)
                {
                    scenes[i]->sounds[j]->disableAllEvents();
                }
                scenes[i]->disable();
            }
        }

        addScene->y = y + config.scene_spacing;

        if(scenes.size() >= config.max_scenes) {
            addScene->disableEvents();
        }
        addScene->doAddScene = false;
    }

    //Delete scene
    if(scenes[config.activeSceneIdx]->delete_scene.doDeleteScene)
    {
        scenes[config.activeSceneIdx]->delete_scene.doDeleteScene = false;

        size_t scene_to_delete = config.activeSceneIdx;

        for(size_t j = 0; j < scenes[scene_to_delete]->sounds.size();j++)
        {
            scenes[scene_to_delete]->sounds[j]->isSetup = false;
        }
        delete scenes[scene_to_delete];
        scenes.erase(scenes.begin()+scene_to_delete);
        //cout << "delete scene " << scene_to_delete << endl;

        config.activeScene = scenes[0]->id;
        enableScene(0);

        if(scenes.size() < config.max_scenes) {
            addScene->enableEvents();
        }

        updateScenePosition();
    }
}

void ofApp::updateScenePosition()
{
    for(size_t i=0;i < scenes.size();i++) {
        int y = i* config.scene_spacing + config.yoffset;
        scenes[i]->updatePosition(scenes[i]->x,y);
    }

    int y = scenes.size() * config.scene_spacing + config.yoffset;
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

    mainVolume.render();
}

//--------------------------------------------------------------
void ofApp::keyPressed  (ofKeyEventArgs & args){
    int key = args.key;

    if(key == 'q' && args.hasModifier(OF_KEY_CONTROL)){
        ofExit();
    }
    if(key == ' ') {
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

}
