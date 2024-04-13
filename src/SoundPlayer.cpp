#include "SoundPlayer.h"
#include "OpenALSoundPlayer.h"

SoundPlayer::SoundPlayer()
{
    curSound = 0;
    AudioSample s;
    s.audioPlayer = new OpenALSoundPlayer();
    s.id = 0;
    player.push_back(s);

    bPlayingDelay = false;
    prevTime = curTime = ofGetElapsedTimeMillis();
    bPaused = true;    
    bPlayBackEnded = false;
    bCheckPlayBackEnded = false;
    minDelay = 0;
    maxDelay = 0;

    ofAddListener(OpenALSoundPlayer::playbackEnded, this, &SoundPlayer::playbackEnded);
}

//--------------------------------------------------------------
SoundPlayer::~SoundPlayer()
{
    close();    
    ofRemoveListener(OpenALSoundPlayer::playbackEnded, this, &SoundPlayer::playbackEnded);
}

//--------------------------------------------------------------
void SoundPlayer::close()
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
    player[0].config = _config;
    player[0].setWidth(config->sample_gui_width);
    player[0].setHeight(35*config->y_scale);
    id = _id;
}

//--------------------------------------------------------------
SoundPlayer::SoundPlayer(const SoundPlayer& parent) {
    ofLogVerbose() << "SoundPlayer copy constructor called";


}

//--------------------------------------------------------------
void SoundPlayer::update()
{
//    if(bCheckPlayBackEnded) {
//        if (!(player[curSound].audioPlayer->isPlaying())) {
//            bPlayBackEnded = true;
//            bCheckPlayBackEnded = false;
//        }
//    }

    if(bPlayBackEnded) {
        cout << "playback ended! current sound: " << curSound << " ID = " << id << endl;
        if(curSound < player.size()-1) {
            curSound++;
            recalculateDelay(curSound);
            setPaused(false);
            cout << "play new sound: " << curSound << " ID = " << id << endl;
        } else {
            curSound = 0;
        }

        if( !bPaused && !bPlayingDelay) {
            if(player[curSound].totalDelay > 0) {
                recalculateDelay(curSound);
            }
            if(bIsLooping) {
                //if(player[curSound].totalDelay > 0) {
                    bPlayingDelay = true;
                //}
            } else {
                bPaused = true;
            }
        }
        bPlayBackEnded = false;
    }

    curTime = ofGetElapsedTimeMillis();
    int diff = curTime - prevTime;
    if(bPlayingDelay)
    {
        if(!bPaused) {
            player[curSound].curDelay -= diff;
            if(player[curSound].curDelay <= 0) {
                player[curSound].curDelay = 0;
                cout << "player["<<curSound<<"].setPaused(false)" << endl;
                player[curSound].audioPlayer->setPaused(false);
                bCheckPlayBackEnded = true;
                bPlayingDelay = false;
            }
        }
    }
    prevTime = curTime;
}

//--------------------------------------------------------------------
void SoundPlayer::play(){
   player[curSound].audioPlayer->play();
   bCheckPlayBackEnded = true;
}

//--------------------------------------------------------------
void SoundPlayer::stop()
{
    if(bPlayingDelay) {
        //bPlayingDelay = false;
        //player[curSound].curDelay = 0;
    } else {
        player[curSound].audioPlayer->stop();
    }
    bCheckPlayBackEnded = false;
    curSound = 0;
    recalculateDelay(curSound);
    bPaused = true;
}

//--------------------------------------------------------------------
bool SoundPlayer::load(const std::filesystem::path& fileName, bool stream)
{
    return load(fileName, 0, stream);
}

//--------------------------------------------------------------------
bool SoundPlayer::load(const std::filesystem::path& fileName, int idx, bool stream)
{
    bool bResult = player[idx].audioPlayer->load(fileName, stream);
    return bResult;

}

//--------------------------------------------------------------------
void SoundPlayer::recalculateDelay(int id)
{   
    player[id].curDelay = player[id].totalDelay = ofRandom(minDelay, maxDelay);
    //cout << "recalculate delay for cursound[" << id << "] = " << player[id].totalDelay << endl;
    if(player[id].totalDelay > 0) {
        player[id].audioPlayer->setLoop(false);
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
        bPlayingDelay = !bPaused;
        //cout << "bPlayingDelay = " << bPlayingDelay << endl;
    } else {
        player[curSound].audioPlayer->setPaused(bPaused);
        if(bPaused) {
            bCheckPlayBackEnded = false;
        } else {
            bCheckPlayBackEnded = true;
        }
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
            player[curSound].audioPlayer->setLoop(false);
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
    if(bPlayingDelay) {
        player[curSound].curDelay = player[curSound].totalDelay * (1.0f - pct);
    } else {
        player[curSound].audioPlayer->setPosition(pct);
    }
}

//--------------------------------------------------------------------
void SoundPlayer::setPositionMS(int ms)
{
    player[curSound].audioPlayer->setPositionMS(ms);
}

//--------------------------------------------------------------------
void SoundPlayer::setMinDelay(int delay)
{
    minDelay = delay;
}

//--------------------------------------------------------------------
void SoundPlayer::setMaxDelay(int delay)
{
    maxDelay = delay;
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
    //if(player[curSound].curDelay > 0) {
    if(bPlayingDelay) {
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
bool SoundPlayer::isLooping() const
{
    return player[curSound].audioPlayer->isLooping();
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
int SoundPlayer::getCurSound() const
{
    return curSound;
}

//--------------------------------------------------------------------
bool SoundPlayer::getIsStereo() const
{
    return player[curSound].audioPlayer->getIsStereo();
}

//--------------------------------------------------------------------
int SoundPlayer::getMinDelay() const
{
    return minDelay;
}

//--------------------------------------------------------------------
int SoundPlayer::getMaxDelay() const
{
    return maxDelay;
}

//--------------------------------------------------------------------
int SoundPlayer::getTotalDelay() const
{
    return player[curSound].totalDelay;
}

//--------------------------------------------------------------------
float SoundPlayer::getReverbSend() const
{
    return player[curSound].audioPlayer->getReverbSend();
}

//--------------------------------------------------------------------
void SoundPlayer::setReverbSend(float send)
{
    for(int i = 0; i < player.size();i++) {
        player[i].audioPlayer->setReverbSend(send);
    }

}

//--------------------------------------------------------------------
void SoundPlayer::playbackEnded(OpenALSoundPlayer* &args)
{
    for(int i = 0; i < player.size();i++)
    {
        if(player[i].audioPlayer == args) {
            bPlayBackEnded = true;
        }
    }

}
