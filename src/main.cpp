#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"
#include "GLFW/glfw3.h"

//========================================================================
int main( ){
    //ofSetLogLevel(OF_LOG_VERBOSE);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "FEEDRA");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "Feedra");
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
