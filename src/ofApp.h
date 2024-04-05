#pragma once

#include "AppConfig.h"
#include "Scene.h"
#include "UI/AddScene.h"
#include "UI/SimpleSlider.h"
#include "UI/CheckBox.h"

class ofApp : public ofBaseApp {

	public:
        ~ofApp();
		void setup();
		void update();
		void draw();

        void keyPressed  (ofKeyEventArgs & args);
        void keyReleased(ofKeyEventArgs & args);
		void mouseMoved(int x, int y );
		void mouseDragged(int x, int y, int button);
		void mousePressed(int x, int y, int button);
		void mouseReleased(int x, int y, int button);
		void mouseEntered(int x, int y);
		void mouseExited(int x, int y);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);
		void gotMessage(ofMessage msg);		        
        void updateScenePosition();
        void enableScene(int i);
        void onClicked(SliderData& args);
        void onObjectClicked(size_t& args);
        void updateSliders();        
        void saveConfig();
        void saveConfig(string path, bool bCopyFiles);
        void loadConfig();
        void loadConfig(string newpath);
        void addNewScene();
        void deleteScene();
        void checkAudioDeviceChange();
        void drawSoundInfo();
        void calculateSources();
        void renderMainPage();
        void renderEditPage();
        void disableEvents();
        void enableEvents();

        static void window_minimise_callback(GLFWwindow* window, int minimised);
        static bool bMinimised;

        int pages[3];
        vector<Scene*> scenes;
        AddScene* addScene;
        int maxScenes;
        AppConfig config;
        SimpleSlider mainVolume;

        SimpleSlider minDelay;
        SimpleSlider maxDelay;
        SimpleSlider pan;
        SimpleSlider reverbSend;

        CheckBox setStereo;

        bool bDoRender;
        bool bLoadScenes;
        bool bClearPad;
        int  doClearPad;

        long int curTime;
        long int prevTime;
        string curDevice;
        string newDevice;

        enum PageState { MAIN, EDIT, SETTINGS };
        PageState pageState;
};

