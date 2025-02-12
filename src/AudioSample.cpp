#include "AudioSample.h"
#include "OpenALSoundPlayer.h"
#include "ofMain.h"

int AudioSample::count = -1;
ofEvent<int> AudioSample::clickedSampleEvent;

AudioSample::~AudioSample()
{
    ofRemoveListener(this->clickedEvent, this, &AudioSample::onClicked);
}
AudioSample::AudioSample()
{
    count++;
    id = 0;
    totalDelay = 0;
    curDelay = 0;
    gain = 1.0f;
    pitch = 1.0f;
    config = nullptr;
    bEditorMode = false;
    bSelected = false;
    //cout << "constructor, config = " << config << endl;
}

//--------------------------------------------------------------
AudioSample::AudioSample(const AudioSample& parent)
{
    id = parent.id;
    totalDelay = parent.totalDelay;
    curDelay = parent.curDelay;
    gain = parent.gain;
    pitch = parent.pitch;
    config = parent.config;
    bEditorMode = parent.bEditorMode;
    bSelected = parent.bSelected;

    sample_path = parent.sample_path;
    audioPlayer = parent.audioPlayer;

    setWidth(parent.getWidth());
    setHeight(parent.getHeight());
    setX(parent.x);
    setY(parent.y);
    cout << "copy constructor, id = " << id << " sample_path = " << sample_path << endl;
}
//--------------------------------------------------------------
void AudioSample::setup()
{
    ofAddListener(this->clickedEvent, this, &AudioSample::onClicked);
}

//--------------------------------------------------------------
void AudioSample::setPitch(float val)
{
    //cout << "set pitch = " << val << endl;
    pitch = val;
    audioPlayer->setSpeed(val);
}

//--------------------------------------------------------------
void AudioSample::setGain(float val)
{
    gain = val;
}

//--------------------------------------------------------------
void AudioSample::render(ofVec3f pos)
{
    setPosition(pos);

    ofPushStyle();

    ofSetColor(255);
    ofDrawRectRounded(getPosition(), getWidth(),getHeight(),config->x_scale*5);

    if(audioPlayer->isPlaying())
    {
        ofSetColor(200);
    } else {
        ofSetColor(255);
    }
    ofDrawRectRounded(pos, config->sample_gui_width*audioPlayer->getPosition(),35*config->y_scale,config->x_scale*5);
    if(config->activeSample == id) {
        ofNoFill();
        ofSetColor(255,0,0);
        ofSetLineWidth(2*config->x_scale);
        ofDrawRectRounded(getPosition(), getWidth(),getHeight(),config->x_scale*5);
    }

    ofSetColor(50);
    std::filesystem::path p(sample_path);
    string name = p.filename().string() +  " id = "+ofToString(id); //audioPlayer->getFormatString()
    config->f2().drawString(name,pos.x+20*config->x_scale,pos.y+22*config->y_scale);

    ofPopStyle();
}

//--------------------------------------------------------------
void AudioSample::enableEditorMode()
{
    if(!bEditorMode) {
        enableEvents();
        bEditorMode = true;
    }
}

//--------------------------------------------------------------
void AudioSample::disableEditorMode()
{
    if(bEditorMode) {
        disableEvents();
        bEditorMode = false;
    }
}

//--------------------------------------------------------------
void AudioSample::onClicked(ClickArgs& args) {
    if(!bEditorMode) return;

   //ofLogNotice() << "Clicked " << id << " AudioSample: " << sample_path;
    config->activeSample = id;

    ofNotifyEvent(clickedSampleEvent,  id);
}
