#pragma once

#include "ofMain.h"
#include "AppConfig.h"
#include "SimpleSlider.h"
#include "Scene.h"
#include "AddScene.h"
#include "DeleteScene.h"

class ofApp : public ofBaseApp {

	public:
        ~ofApp();
		void setup();
		void update();
		void draw();

        void keyPressed  (ofKeyEventArgs & args);
		void keyReleased(int key);
		void mouseMoved(int x, int y );
		void mouseDragged(int x, int y, int button);
		void mousePressed(int x, int y, int button);
		void mouseReleased(int x, int y, int button);
		void mouseEntered(int x, int y);
		void mouseExited(int x, int y);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);
		void gotMessage(ofMessage msg);		        
        void loadScenes();
        void updateScenePosition();
        void enableScene(int i);
        void onClicked(SliderData& args);
        void onObjectClicked(int& args);
        void updateSliders();
        void saveConfig();
        void saveConfig(string path);
        void addNewScene();
        void deleteScene();
        void checkAudioDeviceChange();

        vector<Scene*> scenes;
        AddScene* addScene;
        int maxScenes;
        AppConfig config;
        SimpleSlider mainVolume;

        SimpleSlider minDelay;
        SimpleSlider maxDelay;
        SimpleSlider pan;

        bool bDoRender;

        long int curTime;
        long int prevTime;
        string curDevice;
        string newDevice;
};

