#include "SoundPlayer.h"
#include "OpenALSoundPlayer.h"

SoundPlayer::SoundPlayer()
{
    curSound = 0;
//    AudioSample s;
//    s.audioPlayer = new OpenALSoundPlayer();
//    s.id = 0;
//    player.push_back(s);

    bPlayingDelay = false;
    prevTime = curTime = ofGetElapsedTimef();
    bPaused = true;    
    bPlayBackEnded = false;
    bCheckPlayBackEnded = false;
    bRandomPlayback = false;
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
    //ofLogNotice() << "SoundPlayer destructor called...id: " << id;
    for(int i = 0;i < player.size();i++)
    {
        delete player[i]->audioPlayer;
    }
    for(int i = 0;i < player.size();i++)
    {
        delete player[i];
    }
    player.clear();
}

//--------------------------------------------------------------
void SoundPlayer::setup(AppConfig* _config, int _id)
{

    config = _config;
    if(player.size() > 0) {
        player[0]->config = _config;
        player[0]->setWidth(config->sample_gui_width);
        player[0]->setHeight(35*config->y_scale);
    }
    id = _id;
}

//--------------------------------------------------------------
SoundPlayer::SoundPlayer(const SoundPlayer& parent) {
    ofLogVerbose() << "SoundPlayer copy constructor called";


}

//--------------------------------------------------------------
void SoundPlayer::update()
{
    if(bPlayBackEnded) {
        cout << "playback ended! current sound: " << curSound << " ID = " << id << endl;
        if(curSound < player.size()-1) {
            if(bRandomPlayback) {
                int idx = ofRandom(0,player.size());
                while(idx == curSound) {
                    idx = ofRandom(0,player.size());
                }
                curSound = idx;
            } else {
                curSound++;
            }
            if (bRandomPan) {
                player[curSound]->setPan(ofRandomf());
            }
            recalculateDelay(curSound);
            setPaused(false);
            cout << "play new sound: " << curSound << " ID = " << id << endl;
        } else {
            curSound = 0;
        }

        if( !bPaused && !bPlayingDelay) {
            if(player[curSound]->totalDelay > 0) {
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

    curTime = ofGetElapsedTimef();
    float diffTime = curTime - prevTime;
    if(bPlayingDelay)
    {
        if(!bPaused) {
            player[curSound]->curDelay -= diffTime;
            if(player[curSound]->curDelay <= 0) {
                player[curSound]->curDelay = 0;
                cout << "player["<<curSound<<"].setPaused(false)" << endl;
                player[curSound]->audioPlayer->setPaused(false);
                bCheckPlayBackEnded = true;
                bPlayingDelay = false;
            }
        }
    }
    prevTime = curTime;
}

//--------------------------------------------------------------------
void SoundPlayer::play(){
   player[curSound]->audioPlayer->play();
   bCheckPlayBackEnded = true;
}

//--------------------------------------------------------------
void SoundPlayer::stop()
{
    if(player.size() == 0) return;

    if(bPlayingDelay) {
        //bPlayingDelay = false;
        //player[curSound].curDelay = 0;
    } else {
        player[curSound]->audioPlayer->stop();
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
    bool bResult = player[idx]->audioPlayer->load(fileName, stream);
    return bResult;

}

//--------------------------------------------------------------------
void SoundPlayer::recalculateDelay(int id)
{   
    if(player.size() == 0) return;

    player[id]->totalDelay = ofRandom(minDelay, maxDelay);
    player[id]->curDelay = player[id]->totalDelay;
    //cout << "recalculate delay for cursound[" << id << "] = " << player[id].totalDelay << endl;
    if(player[id]->totalDelay > 0) {
        player[id]->audioPlayer->setLoop(false);
    }
}

//--------------------------------------------------------------------
void SoundPlayer::unload(){
    if(player.size() == 0) return;

    player[curSound]->audioPlayer->unload();
}

//--------------------------------------------------------------------
void SoundPlayer::setPaused(bool _bPause){
    if(player.size() == 0) return;

    bPaused = _bPause;
    if(player[curSound]->curDelay > 0) {
        bPlayingDelay = !bPaused;
        //cout << "bPlayingDelay = " << bPlayingDelay << endl;
    } else {
        player[curSound]->audioPlayer->setPaused(bPaused);
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
    if(player.size() == 0) return;

    player[curSound]->audioPlayer->setVolume(vol);
}

//--------------------------------------------------------------
void SoundPlayer::setPan(float pan)
{
    if(player.size() == 0) return;

    player[curSound]->audioPlayer->setPan(glm::clamp(pan,-1.0f,1.0f));
}

//--------------------------------------------------------------------
void SoundPlayer::setSpeed(float spd){
    if(player.size() == 0) return;

    player[curSound]->audioPlayer->setSpeed(spd);
}

//--------------------------------------------------------------------
 void SoundPlayer::setLoop(bool bLoop){
    if(player.size() == 0) return;

    bIsLooping = bLoop;

    if(bIsLooping) {
        if(player[curSound]->totalDelay > 0) {
            player[curSound]->audioPlayer->setLoop(false);
        } else {
            player[curSound]->audioPlayer->setLoop(false);
        }
    } else {
        player[curSound]->audioPlayer->setLoop(bLoop);
    }

}

 //--------------------------------------------------------------------
 float SoundPlayer::getDuration() const
 {
     if(player.size() == 0) return 0;

     return player[curSound]->audioPlayer->getDuration();
 }

//--------------------------------------------------------------------
void SoundPlayer::setPosition(float pct)
{
    if(player.size() == 0) return;

    if(bPlayingDelay) {
        player[curSound]->curDelay = player[curSound]->totalDelay * (1.0f - pct);
    } else {
        player[curSound]->audioPlayer->setPosition(pct);
    }
}

//--------------------------------------------------------------------
void SoundPlayer::setPositionMS(int ms)
{
    if(player.size() == 0) return;

    player[curSound]->audioPlayer->setPositionMS(ms);
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
    if(player.size() == 0) return 0;

    if(isPlayingDelay()) {
        float remain =   1.0f - (player[curSound]->curDelay/player[curSound]->totalDelay);
        //cout << "remain = " << remain << endl;
        if(remain <= 0) remain = 0.0000001f;
        return remain;
    }
    return player[curSound]->audioPlayer->getPosition();
}

//--------------------------------------------------------------------
int SoundPlayer::getPositionMS() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getPositionMS();
}

//--------------------------------------------------------------------
bool SoundPlayer::isPlaying() const
{
    if(player.size() == 0) return false;
    ;
    if(bPlayingDelay) {
        if(bPaused) {
            return false;
        } else {
            return true;
        }
    }
    return player[curSound]->audioPlayer->isPlaying();
}

//--------------------------------------------------------------------
bool SoundPlayer::isPlayingDelay() const
{
    if(bPlayingDelay) {
        return true;
    }

    return false;
}

//--------------------------------------------------------------------
bool SoundPlayer::isLoaded() const
{
    if(player.size() == 0) return false;

    return player[curSound]->audioPlayer->isLoaded();
}

//--------------------------------------------------------------------
bool SoundPlayer::isLooping() const
{
    if(player.size() == 0) return false;

    return player[curSound]->audioPlayer->isLooping();
}

//--------------------------------------------------------------------
float SoundPlayer::getSpeed() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getSpeed();
}

//--------------------------------------------------------------------
float SoundPlayer::getPan() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getPan();
}

//--------------------------------------------------------------------
float SoundPlayer::getVolume() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getVolume();
}

//--------------------------------------------------------------------
int SoundPlayer::getSampleRate() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getSampleRate();
}

//--------------------------------------------------------------------
int SoundPlayer::getNumChannels() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getNumChannels();
}

//--------------------------------------------------------------------
int SoundPlayer::getCurSound() const
{
    return curSound;
}

//--------------------------------------------------------------------
bool SoundPlayer::getIsTrueStereo() const
{
    if(player.size() == 0) return false;

    return player[curSound]->audioPlayer->getIsTrueStereo();
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
float SoundPlayer::getTotalDelay() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->totalDelay;
}

//--------------------------------------------------------------------
float SoundPlayer::getReverbSend() const
{
    if(player.size() == 0) return 0;

    return player[curSound]->audioPlayer->getReverbSend();
}

//--------------------------------------------------------------------
void SoundPlayer::setReverbSend(float send)
{
    if(player.size() == 0) return;

    for(int i = 0; i < player.size();i++) {
        player[i]->audioPlayer->setReverbSend(send);
    }

}

//--------------------------------------------------------------------
void SoundPlayer::playbackEnded(OpenALSoundPlayer* &args)
{
    if(player.size() == 0) return;

    for(int i = 0; i < player.size();i++)
    {
        if(player[i]->audioPlayer == args) {
            bPlayBackEnded = true;
        }
    }

}
