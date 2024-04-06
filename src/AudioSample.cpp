#include "AudioSample.h"
#include "OpenALSoundPlayer.h"
#include "ofMain.h"

AudioSample::AudioSample()
{
    totalDelay = 0;
    curDelay = 0;
    gain = 1.0f;
    pitch = 1.0f;
    config = nullptr;
    //cout << "constructor, config = " << config << endl;
}


AudioSample::AudioSample(const AudioSample& parent)
{
    totalDelay = parent.totalDelay;
    curDelay = parent.curDelay;
    gain = parent.gain;
    pitch = parent.gain;
    config = parent.config;
    //cout << "copy constructor, config = " << config << endl;
    sample_path = parent.sample_path;
    audioPlayer = parent.audioPlayer;
}

void AudioSample::render(ofVec3f pos)
{

    ofPushStyle();

    ofSetColor(255);
    ofDrawRectRounded(pos, config->sample_gui_width,35*config->y_scale,config->x_scale*5);

    if(audioPlayer->isPlaying())
    {
        ofSetColor(200);
    } else {
        ofSetColor(255);
    }
    ofDrawRectRounded(pos, config->sample_gui_width*audioPlayer->getPosition(),35*config->y_scale,config->x_scale*5);

    ofSetColor(50);
    std::filesystem::path p(sample_path);
    string name = p.filename().string(); //audioPlayer->getFormatString()
    config->f2().drawString(name,pos.x+20*config->x_scale,pos.y+22*config->y_scale);

    ofPopStyle();
}
