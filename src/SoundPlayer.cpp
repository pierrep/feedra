#include "SoundPlayer.h"
#include "OpenALSoundPlayer.h"

#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

SoundPlayer::SoundPlayer(QObject* parent)
    : QObject(parent)
{
    clock.start();
    prevMs = clock.elapsed();
    OpenALSoundPlayer::addPlaybackEndedListener(this, [this](OpenALSoundPlayer* ended) {
        onPlaybackEnded(ended);
    });
}

SoundPlayer::~SoundPlayer()
{
    OpenALSoundPlayer::removePlaybackEndedListener(this);
    close();
}

void SoundPlayer::close()
{
    for (AudioSample* sample : player) {
        delete sample;
    }
    player.clear();
}

void SoundPlayer::setup(AppConfig* conf, int newId)
{
    config = conf;
    id = newId;
}

void SoundPlayer::update()
{
    if (bPlayBackEnded) {
        bStartFromBeginning = true;
        if (curSound < static_cast<int>(player.size()) - 1) {
            if (bRandomPlayback && player.size() > 1) {
                int idx = curSound;
                while (idx == curSound) {
                    idx = static_cast<int>(randomRange(0.0f, static_cast<float>(player.size())));
                    idx = std::clamp(idx, 0, static_cast<int>(player.size()) - 1);
                }
                curSound = idx;
            } else {
                curSound++;
            }
            recalculateDelay(curSound);
            setPaused(false);
        } else {
            curSound = 0;
        }

        if (!bPaused && !bPlayingDelay) {
            if (player[curSound]->totalDelay > 0) {
                recalculateDelay(curSound);
            }
            if (bIsLooping) {
                bPlayingDelay = true;
            } else {
                bPaused = true;
            }
        }
        bPlayBackEnded = false;
    }

    const qint64 now = clock.elapsed();
    const float diffTime = static_cast<float>(now - prevMs) / 1000.0f;
    if (bPlayingDelay && !bPaused) {
        player[curSound]->curDelay -= diffTime;
        if (player[curSound]->curDelay <= 0) {
            player[curSound]->curDelay = 0;
            applyRandomPanOnStart();
            applyVolumeToCurrent();
            player[curSound]->audioPlayer->setPaused(false);
            bCheckPlayBackEnded = true;
            bPlayingDelay = false;
        }
    }
    prevMs = now;
}

void SoundPlayer::play()
{
    if (player.empty()) {
        return;
    }
    applyRandomPanOnStart();
    applyVolumeToCurrent();
    player[curSound]->audioPlayer->play();
    bCheckPlayBackEnded = true;
}

void SoundPlayer::stop()
{
    if (player.empty()) {
        return;
    }
    if (!bPlayingDelay) {
        player[curSound]->audioPlayer->stop();
    }
    bCheckPlayBackEnded = false;
    curSound = 0;
    recalculateDelay(curSound);
    bPaused = true;
    bStartFromBeginning = true;
}

void SoundPlayer::playSample(int index)
{
    if (index < 0 || index >= static_cast<int>(player.size())) {
        return;
    }
    if (index == curSound && isPlaying()) {
        return;
    }
    setPaused(true);
    player[curSound]->audioPlayer->stop();
    curSound = index;
    player[curSound]->audioPlayer->stop();
    bStartFromBeginning = true;
    setPaused(false);
}

bool SoundPlayer::load(const std::filesystem::path& fileName, bool stream)
{
    return load(fileName, 0, stream);
}

bool SoundPlayer::load(const std::filesystem::path& fileName, int idx, bool stream)
{
    if (idx < 0 || idx >= static_cast<int>(player.size())) {
        return false;
    }
    return player[idx]->audioPlayer->load(fileName, stream);
}

void SoundPlayer::recalculateDelay(int delayId)
{
    if (player.empty() || delayId < 0 || delayId >= static_cast<int>(player.size())) {
        return;
    }
    player[delayId]->totalDelay = randomRange(static_cast<float>(minDelay), static_cast<float>(maxDelay));
    player[delayId]->curDelay = player[delayId]->totalDelay;
    if (player[delayId]->totalDelay > 0) {
        player[delayId]->audioPlayer->setLoop(false);
    }
}

void SoundPlayer::unload()
{
    if (player.empty()) {
        return;
    }
    player[curSound]->audioPlayer->unload();
}

void SoundPlayer::setPaused(bool pause)
{
    if (player.empty()) {
        return;
    }
    bPaused = pause;
    if (player[curSound]->curDelay > 0) {
        bPlayingDelay = !bPaused;
    } else {
        if (!bPaused) {
            applyRandomPanOnStart();
            // A sample that has never been the current one still has OpenAL's default
            // gain of 1.0; without this it would start at full volume until the next tick.
            applyVolumeToCurrent();
        }
        player[curSound]->audioPlayer->setPaused(bPaused);
        bCheckPlayBackEnded = !bPaused;
    }
}

void SoundPlayer::setBaseVolume(float vol)
{
    baseVolume = vol;
    applyVolumeToCurrent();
}

void SoundPlayer::applyVolumeToCurrent()
{
    if (player.empty() || baseVolume < 0.0f || curSound < 0 || curSound >= static_cast<int>(player.size())) {
        return;
    }
    AudioSample* sample = player[static_cast<size_t>(curSound)];
    sample->audioPlayer->setVolume(baseVolume * sample->getGain());
}

void SoundPlayer::setVolume(float vol)
{
    if (player.empty()) {
        return;
    }
    player[curSound]->audioPlayer->setVolume(vol);
}

void SoundPlayer::setPan(float pan)
{
    if (player.empty()) {
        return;
    }
    player[curSound]->audioPlayer->setPan(std::clamp(pan, -1.0f, 1.0f));
}

void SoundPlayer::setSpeed(float spd)
{
    if (player.empty()) {
        return;
    }
    player[curSound]->audioPlayer->setSpeed(spd);
}

void SoundPlayer::setLoop(bool bLoop)
{
    if (player.empty()) {
        return;
    }
    bIsLooping = bLoop;
    player[curSound]->audioPlayer->setLoop(false);
}

float SoundPlayer::getDuration() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getDuration();
}

void SoundPlayer::setPosition(float pct)
{
    if (player.empty()) {
        return;
    }
    if (bPlayingDelay) {
        player[curSound]->curDelay = player[curSound]->totalDelay * (1.0f - pct);
    } else {
        player[curSound]->audioPlayer->setPosition(pct);
    }
}

void SoundPlayer::seekTo(float pct)
{
    if (player.empty()) {
        return;
    }
    if (bPlayingDelay) {
        player[curSound]->curDelay = player[curSound]->totalDelay * (1.0f - pct);
    } else if (player[curSound]->audioPlayer) {
        player[curSound]->audioPlayer->seekTo(pct);
    }
}

void SoundPlayer::setPositionMS(int ms)
{
    if (player.empty()) {
        return;
    }
    player[curSound]->audioPlayer->setPositionMS(ms);
}

void SoundPlayer::setMinDelay(int delay)
{
    minDelay = delay;
}

void SoundPlayer::setMaxDelay(int delay)
{
    maxDelay = delay;
}

float SoundPlayer::getPosition() const
{
    if (player.empty()) {
        return 0;
    }
    if (isPlayingDelay()) {
        float remain = 1.0f - (player[curSound]->curDelay / player[curSound]->totalDelay);
        if (remain <= 0) {
            remain = 0.0000001f;
        }
        return remain;
    }
    return player[curSound]->audioPlayer->getPosition();
}

int SoundPlayer::getPositionMS() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getPositionMS();
}

bool SoundPlayer::isPlaying() const
{
    if (player.empty()) {
        return false;
    }
    if (bPlayingDelay) {
        return !bPaused;
    }
    return player[curSound]->audioPlayer->isPlaying();
}

bool SoundPlayer::isPlayingDelay() const
{
    return bPlayingDelay;
}

bool SoundPlayer::isLoaded() const
{
    if (player.empty()) {
        return false;
    }
    return player[curSound]->audioPlayer->isLoaded();
}

bool SoundPlayer::isLooping() const
{
    return bIsLooping;
}

float SoundPlayer::getSpeed() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getSpeed();
}

float SoundPlayer::getPan() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getPan();
}

float SoundPlayer::getVolume() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getVolume();
}

int SoundPlayer::getSampleRate() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getSampleRate();
}

int SoundPlayer::getNumChannels() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getNumChannels();
}

int SoundPlayer::getCurSound() const
{
    return curSound;
}

int SoundPlayer::getMinDelay() const
{
    return minDelay;
}

int SoundPlayer::getMaxDelay() const
{
    return maxDelay;
}

float SoundPlayer::getTotalDelay() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->totalDelay;
}

float SoundPlayer::getReverbSend() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getReverbSend();
}

void SoundPlayer::setReverbSend(float send)
{
    for (AudioSample* sample : player) {
        sample->audioPlayer->setReverbSend(send);
    }
}

float SoundPlayer::getReverbSend2() const
{
    if (player.empty()) {
        return 0;
    }
    return player[curSound]->audioPlayer->getReverbSend2();
}

void SoundPlayer::setReverbSend2(float send)
{
    for (AudioSample* sample : player) {
        sample->audioPlayer->setReverbSend2(send);
    }
}

void SoundPlayer::applyRandomPanOnStart()
{
    if (!bStartFromBeginning) {
        return;
    }
    bStartFromBeginning = false;
    if (!bRandomPan || player.empty()) {
        return;
    }
    AudioSample* sample = player[curSound];
    if (!sample->audioPlayer->canPan()) {
        return;
    }
    sample->setPan(randomF());
    emit panRandomised(curSound, sample->getPan());
}

void SoundPlayer::onPlaybackEnded(OpenALSoundPlayer* ended)
{
    for (AudioSample* sample : player) {
        if (sample->audioPlayer == ended) {
            bPlayBackEnded = true;
        }
    }
}

float SoundPlayer::randomRange(float minV, float maxV) const
{
    if (maxV <= minV) {
        return minV;
    }
    return minV + static_cast<float>(QRandomGenerator::global()->generateDouble()) * (maxV - minV);
}

float SoundPlayer::randomF() const
{
    return static_cast<float>(QRandomGenerator::global()->generateDouble() * 2.0 - 1.0);
}
