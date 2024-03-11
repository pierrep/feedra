#include "SoundPlayer.h"

SoundPlayer::SoundPlayer()
{
    curSound = 0;
    AudioSample s;
    s.minDelay = 0;
    s.maxDelay = 0;
    s.totalDelay = 0;
    s.curDelay = 0;
    s.audioPlayer = new OpenALSoundPlayer();
    player.push_back(s);

    bPlayingDelay = false;
    prevTime = curTime = ofGetElapsedTimeMillis();
    bPaused = true;    
}

//--------------------------------------------------------------
SoundPlayer::~SoundPlayer()
{
    ofLogVerbose() << "SoundPlayer destructor called...";
    for(int i = 0;i < player.size();i++)
    {
        delete player[i].audioPlayer;
    }
    player.clear();
}

//--------------------------------------------------------------
void SoundPlayer::setup(AppConfig* _config, int _id)
{

    config = _config;
    id = _id;

    for(int i = 0; i < player.size();i++) {
        player[i].totalDelay = ofRandom(player[i].minDelay, player[i].maxDelay);
        player[i].curDelay = player[i].totalDelay;
        //cout << "new maxDelay = " <<  player[i].totalDelay << endl;
    }
}

//--------------------------------------------------------------
SoundPlayer::SoundPlayer(const SoundPlayer& parent) {
    ofLogVerbose() << "SoundPlayer copy constructor called";


}

//--------------------------------------------------------------
void SoundPlayer::update()
{
    if( !bPaused && !bPlayingDelay && player[curSound].audioPlayer->isStreamEnd()) {
        if(player[curSound].totalDelay > 0) {
            recalculateDelay();
            //cout << "new maxDelay = " <<  player[curSound].totalDelay << endl;
        }
        if(bIsLooping) {
            if(player[curSound].totalDelay > 0) {
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
            //cout << "player[curSound].curDelay = " << player[curSound].curDelay << " diff = " << diff << endl;
            player[curSound].curDelay -= diff;
            if(player[curSound].curDelay <= 0) {
                player[curSound].curDelay = 0;
                player[curSound].audioPlayer->setPaused(false);
                bPlayingDelay = false;
            }
        }
    }
    prevTime = curTime;
}

//--------------------------------------------------------------------
void SoundPlayer::play(){
   player[curSound].audioPlayer->play();
}

//--------------------------------------------------------------
void SoundPlayer::stop()
{
    if(bPlayingDelay) {
        bPlayingDelay = false;
    } else {
        player[curSound].audioPlayer->stop();
    }
    recalculateDelay();
    //cout << "STOP! new totalDelay = " <<  player[curSound].totalDelay << endl;
    bPaused = true;
}

//--------------------------------------------------------------------
bool SoundPlayer::load(const std::filesystem::path& fileName, bool stream)
{
    return load(fileName, 0, stream);
    return player[curSound].audioPlayer->load(fileName, stream);
}

//--------------------------------------------------------------------
bool SoundPlayer::load(const std::filesystem::path& fileName, int idx, bool stream)
{
    return player[idx].audioPlayer->load(fileName, stream);
}

//--------------------------------------------------------------------
void SoundPlayer::recalculateDelay()
{
    player[curSound].curDelay = player[curSound].totalDelay = ofRandom(player[curSound].minDelay, player[curSound].maxDelay);
    if(player[curSound].totalDelay > 0) {
        player[curSound].audioPlayer->setLoop(false);
    }
}

//--------------------------------------------------------------------
void SoundPlayer::unload(){
    player[curSound].audioPlayer->unload();
}

//--------------------------------------------------------------------
void SoundPlayer::setPaused(bool _bPause){
    bPaused = _bPause;
    if(player[curSound].curDelay > 0) {
        bPlayingDelay = !_bPause;
    } else {
        player[curSound].audioPlayer->setPaused(_bPause);
    }

}

//--------------------------------------------------------------------
void SoundPlayer::setVolume(float vol)
{
    player[curSound].audioPlayer->setVolume(vol);
}

//--------------------------------------------------------------
void SoundPlayer::setPan(float pan)
{
    player[curSound].audioPlayer->setPan(glm::clamp(pan,-1.0f,1.0f));
}

//--------------------------------------------------------------------
void SoundPlayer::setSpeed(float spd){
    player[curSound].audioPlayer->setSpeed(spd);
}

//--------------------------------------------------------------------
 void SoundPlayer::setLoop(bool bLoop){
    bIsLooping = bLoop;

    if(bIsLooping) {
        if(player[curSound].totalDelay > 0) {
            player[curSound].audioPlayer->setLoop(false);
        } else {
            player[curSound].audioPlayer->setLoop(bLoop);
        }
    } else {
        player[curSound].audioPlayer->setLoop(bLoop);
    }

}

 //--------------------------------------------------------------------
 float SoundPlayer::getDuration() const
 {
     return player[curSound].audioPlayer->getDuration();
 }

//--------------------------------------------------------------------
void SoundPlayer::setPosition(float pct)
{
    player[curSound].audioPlayer->setPosition(pct);
}

//--------------------------------------------------------------------
void SoundPlayer::setPositionMS(int ms)
{
    player[curSound].audioPlayer->setPositionMS(ms);
}

//--------------------------------------------------------------------
float SoundPlayer::getPosition() const
{
    if(isPlayingDelay()) {
        float remain =   1.0f - (float)(player[curSound].curDelay/(float) player[curSound].totalDelay);
        //cout << "remain = " << remain << endl;
        if(remain <= 0) remain = 0.0000001f;
        return remain;
    }
    return player[curSound].audioPlayer->getPosition();
}

//--------------------------------------------------------------------
int SoundPlayer::getPositionMS() const
{
    return player[curSound].audioPlayer->getPositionMS();
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
    return player[curSound].audioPlayer->isPlaying();
}

//--------------------------------------------------------------------
bool SoundPlayer::isPlayingDelay() const
{
    if(player[curSound].curDelay > 0) {
        return true;
    }

    return false;
}

//--------------------------------------------------------------------
bool SoundPlayer::isLoaded() const
{
    return player[curSound].audioPlayer->isLoaded();
}

//--------------------------------------------------------------------
float SoundPlayer::getSpeed() const
{
    return player[curSound].audioPlayer->getSpeed();
}

//--------------------------------------------------------------------
float SoundPlayer::getPan() const
{
    return player[curSound].audioPlayer->getPan();
}

//--------------------------------------------------------------------
float SoundPlayer::getVolume() const
{
    return player[curSound].audioPlayer->getVolume();
}

//--------------------------------------------------------------------
int SoundPlayer::getSampleRate() const
{
    return player[curSound].audioPlayer->getSampleRate();
}

//--------------------------------------------------------------------
int SoundPlayer::getNumChannels() const
{
    return player[curSound].audioPlayer->getNumChannels();
}

//--------------------------------------------------------------------
int SoundPlayer::getMinDelay() const
{
    return player[curSound].minDelay;
}

//--------------------------------------------------------------------
int SoundPlayer::getMaxDelay() const
{
    return player[curSound].maxDelay;
}

//--------------------------------------------------------------------
float SoundPlayer::getReverbSend() const
{
    return player[curSound].audioPlayer->getReverbSend();
}

//--------------------------------------------------------------------
void SoundPlayer::setReverbSend(float send)
{
    player[curSound].audioPlayer->setReverbSend(send);
}
