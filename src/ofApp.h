#pragma once

#include "AppConfig.h"
#include "Scene.h"
#include "UI/Interactive.h"
#include "UI/SimpleSlider.h"
#include "UI/CheckBox.h"
#include "UI/NumberBox.h"

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
        void onSliderClicked(SliderData& args);
        void onCheckboxClicked(Interactive::ClickArgs& args);
        void onSoundObjectClicked(size_t& args);
        void onSoundObjectDragged(size_t& args);
        void onSoundObjectReleased(size_t& args);
        void onNumberChanged(NumberBoxData& args);
        void onSampleClicked(int& args);
        void updateMainSliders();
        void updateEditSliders();
        void saveConfig();
        void saveConfig(string path, bool bCopyFiles);
        void loadConfig();
        void loadConfig(string newpath);
        void addNewScene();
        void deleteScene();
        void clearPad();
        void copyPad(int id_from, int id_to);
        void checkAudioDeviceChange();
        void drawSoundInfo();
        void calculateSources();
        void renderMainPage();
        void renderEditPage();
        void disableEvents();
        void enableEvents();
        void enableEditorMode();
        void disableEditorMode();
        void stopAllSounds();

        static void window_minimise_callback(GLFWwindow* window, int minimised);
        static bool bMinimised;

        int pages[3];
        vector<Scene*> scenes;
        Button* addScene;
        int maxScenes;
        AppConfig config;
        SimpleSlider mainVolume;

        //Main UI
        NumberBox minDelay;
        NumberBox maxDelay;
        SimpleSlider reverbSend;
        CheckBox randomPlayback;

        //Edit UI
        SimpleSlider panSlider;
        SimpleSlider gainSlider;
        SimpleSlider pitchSlider;      
        CheckBox randomPan;
        Button* addSample;

        bool bDoRender;
        bool bLoadScenes;
        bool bClearPad;
        bool bClearSample;
        bool bDoDragDrop;
        bool bDrawDragging;
        int draggingStarted;
        bool bDoPlaySample;

        long int curAppTime;
        long int prevAppTime;
        string curDevice;
        string newDevice;

        enum PageState { MAIN, EDIT, SETTINGS };
        PageState pageState;

        bool bLoading;
        bool bLoadingScenes;
        bool bThreadsDone;
        float startLoadTime;
        float endLoadTime;
};

