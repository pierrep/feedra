#include "SoundPlayer.h"

SoundPlayer::SoundPlayer()
{
    curSound = 0;
    OpenALSoundPlayer* p = new OpenALSoundPlayer();
    audioPlayer.push_back(p);
    minDelay.push_back(0);
    maxDelay.push_back(0);
    totalDelay.push_back(ofRandom(minDelay[curSound], maxDelay[curSound]));
    curDelay.push_back(totalDelay[curSound]);
    bPlayingDelay = false;
    prevTime = curTime = ofGetElapsedTimeMillis();
    bPaused = true;    
}

//--------------------------------------------------------------
SoundPlayer::~SoundPlayer()
{
    ofLogVerbose() << "SoundPlayer destructor called...";
    for(int i = 0;i < audioPlayer.size();i++)
    {
        delete audioPlayer[i];
    }
    audioPlayer.clear();
}

//--------------------------------------------------------------
void SoundPlayer::setup(AppConfig* _config, int _id)
{

    config = _config;
    id = _id;

    for(int i = 0; i < audioPlayer.size();i++) {
        totalDelay[i] = (ofRandom(minDelay[i], maxDelay[i]));
        curDelay[i] = totalDelay[i];
        //cout << "new maxDelay = " <<  totalDelay[curSound] << endl;
    }
}

//--------------------------------------------------------------
SoundPlayer::SoundPlayer(const SoundPlayer& parent) {
    ofLogVerbose() << "SoundPlayer copy constructor called";


}

//--------------------------------------------------------------------
void SoundPlayer::play(){
   audioPlayer[curSound]->play();
}

//--------------------------------------------------------------------
void SoundPlayer::setPaused(bool _bPause){
    bPaused = _bPause;
    if(curDelay[curSound] > 0) {
        bPlayingDelay = !_bPause;
    } else {
        audioPlayer[curSound]->setPaused(_bPause);
    }

}

//--------------------------------------------------------------
void SoundPlayer::update()
{
    if( !bPaused && !bPlayingDelay && audioPlayer[curSound]->isStreamEnd()) {
        if(totalDelay[curSound] > 0) {
            recalculateDelay();
            //cout << "new maxDelay = " <<  totalDelay[curSound] << endl;
        }
        if(bIsLooping) {
            if(totalDelay[curSound] > 0) {
                bPlayingDelay = true;
            }
        } else {
            bPaused = true;
        }
    }
    curTime = ofGetElapsedTimeMillis();
    int diff = curTime - prevTime;
    if(bPlayingDelay)
    {
        if(!bPaused) {
            //cout << "curDelay[curSound] = " << curDelay[curSound] << " diff = " << diff << endl;
            curDelay[curSound] -= diff;
            if(curDelay[curSound] <= 0) {
                curDelay[curSound] = 0;
                audioPlayer[curSound]->setPaused(false);
                bPlayingDelay = false;
            }
        }
    }
    prevTime = curTime;
}

//--------------------------------------------------------------
void SoundPlayer::stop()
{
    if(bPlayingDelay) {
        bPlayingDelay = false;
    } else {
        audioPlayer[curSound]->stop();
    }
    recalculateDelay();
    //cout << "STOP! new totalDelay = " <<  totalDelay[curSound] << endl;
    bPaused = true;
}

//--------------------------------------------------------------------
bool SoundPlayer::load(const std::filesystem::path& fileName, bool stream)
{
    return load(fileName, 0, stream);
    return audioPlayer[curSound]->load(fileName, stream);
}

//--------------------------------------------------------------------
bool SoundPlayer::load(const std::filesystem::path& fileName, int idx, bool stream)
{
    return audioPlayer[idx]->load(fileName, stream);
}

//--------------------------------------------------------------------
void SoundPlayer::recalculateDelay()
{
    curDelay[curSound] = totalDelay[curSound] = ofRandom(minDelay[curSound], maxDelay[curSound]);
    if(totalDelay[curSound] > 0) {
        audioPlayer[curSound]->setLoop(false);
    }
}

//--------------------------------------------------------------------
void SoundPlayer::unload(){
    audioPlayer[curSound]->unload();
}

//--------------------------------------------------------------------
void SoundPlayer::setVolume(float vol)
{
    audioPlayer[curSound]->setVolume(vol);
}

//--------------------------------------------------------------
void SoundPlayer::setPan(float pan)
{
    audioPlayer[curSound]->setPan(glm::clamp(pan,-1.0f,1.0f));
}

//--------------------------------------------------------------------
void SoundPlayer::setSpeed(float spd){
    audioPlayer[curSound]->setSpeed(spd);
}

//--------------------------------------------------------------------
 void SoundPlayer::setLoop(bool bLoop){
    bIsLooping = bLoop;

    if(bIsLooping) {
        if(totalDelay[curSound] > 0) {
            audioPlayer[curSound]->setLoop(false);
        } else {
            audioPlayer[curSound]->setLoop(bLoop);
        }
    } else {
        audioPlayer[curSound]->setLoop(bLoop);
    }

}

//--------------------------------------------------------------------
void SoundPlayer::setPosition(float pct)
{
    audioPlayer[curSound]->setPosition(pct);
}

//--------------------------------------------------------------------
void SoundPlayer::setPositionMS(int ms)
{
    audioPlayer[curSound]->setPositionMS(ms);
}

//--------------------------------------------------------------------
float SoundPlayer::getPosition() const
{
    if(isPlayingDelay()) {
        float remain =   1.0f - (float)(curDelay[curSound]/(float) totalDelay[curSound]);
        //cout << "remain = " << remain << endl;
        if(remain <= 0) remain = 0.0000001f;
        return remain;
    }
    return audioPlayer[curSound]->getPosition();
}

//--------------------------------------------------------------------
int SoundPlayer::getPositionMS() const
{
    return audioPlayer[curSound]->getPositionMS();
}

//--------------------------------------------------------------------
bool SoundPlayer::isPlaying() const
{
    if(bPlayingDelay) {
        if(bPaused) {
            return false;
        } else {
            return true;
        }
    }
    return audioPlayer[curSound]->isPlaying();
}

//--------------------------------------------------------------------
bool SoundPlayer::isPlayingDelay() const
{
    if(curDelay[curSound] > 0) {
        return true;
    }

    return false;
}

//--------------------------------------------------------------------
bool SoundPlayer::isLoaded() const
{
    return audioPlayer[curSound]->isLoaded();
}

//--------------------------------------------------------------------
float SoundPlayer::getSpeed() const
{
    return audioPlayer[curSound]->getSpeed();
}

//--------------------------------------------------------------------
float SoundPlayer::getPan() const
{
    return audioPlayer[curSound]->getPan();
}

//--------------------------------------------------------------------
float SoundPlayer::getVolume() const
{
    return audioPlayer[curSound]->getVolume();
}

//--------------------------------------------------------------------
int SoundPlayer::getSampleRate() const
{
    return audioPlayer[curSound]->getSampleRate();
}

//--------------------------------------------------------------------
int SoundPlayer::getNumChannels() const
{
    return audioPlayer[curSound]->getNumChannels();
}
