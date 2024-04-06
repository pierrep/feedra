#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){
    //ofSetLogLevel(OF_LOG_VERBOSE);

#ifdef OF_TARGET_OPENGLES
    ofGLESWindowSettings settings;
    settings.glesVersion=2;
    settings.setSize(1150,800);
#else
    ofGLWindowSettings settings;
    settings.setGLVersion(2,1);
    settings.setSize(1150,800);
#endif
    ofCreateWindow(settings);
    //(GLFWwindow*)ofGetWindowPtr()->setWindowIcon();
#ifdef TARGET_LINUX
    ofAppGLFWWindow* win;
    win = dynamic_cast<ofAppGLFWWindow *> (ofGetWindowPtr());
    win->setWindowIcon("feedra.png");
#endif
	ofRunApp( new ofApp());

}
