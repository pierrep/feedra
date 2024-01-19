#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){
    //ofSetLogLevel(OF_LOG_VERBOSE);

    //ofSetupOpenGL(1024,768, OF_WINDOW);			// <-------- setup the GL context
#ifdef OF_TARGET_OPENGLES
    ofGLESWindowSettings settings;
    settings.glesVersion=2;
    settings.setSize(1024,768);
#else
    ofGLWindowSettings settings;
    settings.setGLVersion(2,1);
    settings.setSize(1150,750);
#endif
    ofCreateWindow(settings);

	ofRunApp( new ofApp());

}
