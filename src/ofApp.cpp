#include "ofApp.h"
#include "GLFW/glfw3.h"

ofApp::~ofApp()
{
    for(size_t i=0;i < scenes.size();i++) {
        delete scenes[i];
    }
    scenes.clear();
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

    config.setup();

    // create scenes
    int maxScenes = 4;
    int scene_width = 200*config.x_scale;
    int scene_height = 50*config.y_scale;
    int baseSceneOffset = config.gridWidth*config.spacing + config.xoffset ;;

    int id = 0;
    for(size_t i = 0; i < maxScenes; i++)
    {
        int x = baseSceneOffset;
        int y = i* config.spacing + config.yoffset;
        Scene* s = new Scene(&config,id,x,y,scene_width,scene_height);
        scenes.push_back(s);
        id++;
    }

    // setup scenes
    for(size_t i=0;i < scenes.size();i++) {
        scenes[i]->setup();
    }

    for(size_t i=0;i < scenes.size();i++) {
        if(config.activeScene != i) {
            for(size_t j = 0; j < scenes[i]->sounds.size();j++)
            {
                scenes[i]->sounds[j]->disableAllEvents();
            }
        }
    }

    mainVolume.setup(-1,config.xoffset,10*config.y_scale,200*config.x_scale,20*config.y_scale,0,1,1,false,false);
    //mainVolume.setLabelString("volume");
}

//--------------------------------------------------------------
void ofApp::update(){

    for(size_t i=0; i < scenes.size();i++) {
         if(scenes[i]->playScene) {
             scenes[i]->playScene = false;

             for(size_t k=0; k < scenes.size();k++) {
                 for(size_t j = 0; j < scenes[i]->sounds.size();j++)
                 {
                     scenes[k]->sounds[j]->disableAllEvents();
                 }
                 scenes[k]->play_button.disableEvents();                 
             }

             for(size_t j = 0; j < scenes[i]->sounds.size();j++)
             {
                 scenes[i]->sounds[j]->enableAllEvents();
             }
             scenes[i]->play_button.enableEvents();
         }
    }

    config.masterVolume = mainVolume.getValue();

    scenes[config.activeScene]->update();
}

//--------------------------------------------------------------
void ofApp::draw(){

    ofBackgroundHex(0x9a8e84);

    for(size_t i=0; i < scenes.size();i++) {
         scenes[i]->render();
     }

}

//--------------------------------------------------------------
void ofApp::keyPressed  (ofKeyEventArgs & args){
    int key = args.key;

    if(key == 'q' && args.hasModifier(OF_KEY_CONTROL)){
        ofExit();
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
