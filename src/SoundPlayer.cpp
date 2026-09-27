#include "SoundPlayer.h"
#include "OpenALSoundPlayer.h"

#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
// How long before the current sample's end the next one is scheduled. Several UI ticks,
// so a busy moment can't make it late, and well inside what the stream queue predicts.
constexpr int64_t kHandoverArmNs = 500'000'000;
// A prediction that moves by more than this (a seek, a pitch change) re-schedules.
constexpr int64_t kHandoverDriftNs = 2'000'000;
}

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
    handover = Handover{};
    {
        std::lock_guard<std::mutex> lock(endedMutex);
        endedQueue.clear();
    }
    heldEnded.clear();
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

int SoundPlayer::chooseNext() const
{
    const int count = static_cast<int>(player.size());
    if (count == 0) {
        return -1;
    }
    if (curSound < count - 1) {
        if (bRandomPlayback && count > 1) {
            int idx = curSound;
            while (idx == curSound) {
                idx = static_cast<int>(randomRange(0.0f, static_cast<float>(count)));
                idx = std::clamp(idx, 0, count - 1);
            }
            return idx;
        }
        return curSound + 1;
    }
    return bRepeat ? 0 : -1;
}

void SoundPlayer::advanceAfterEnd()
{
    bStartFromBeginning = true;
    const int count = static_cast<int>(player.size());
    if (curSound < count - 1) {
        curSound = chooseNext();
        recalculateDelay(curSound);
        // Starts it now, or starts its delay countdown.
        setPaused(false);
        return;
    }
    curSound = 0;
    if (bRepeat) {
        recalculateDelay(curSound);
        bPaused = false;
        bPlayingDelay = true; // counts down (possibly zero), then plays
    } else {
        bPaused = true;
        bPlayingDelay = false;
    }
}

void SoundPlayer::update()
{
    if (handover.armed) {
        checkHandover();
    } else {
        tryArmHandover();
    }

    std::vector<OpenALSoundPlayer*> ended;
    {
        std::lock_guard<std::mutex> lock(endedMutex);
        ended.swap(endedQueue);
    }
    if (!heldEnded.empty()) {
        ended.insert(ended.begin(), heldEnded.begin(), heldEnded.end());
        heldEnded.clear();
    }
    for (OpenALSoundPlayer* audio : ended) {
        handleEnded(audio);
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
    cancelHandover();
    {
        std::lock_guard<std::mutex> lock(endedMutex);
        endedQueue.clear();
    }
    heldEnded.clear();
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
    cancelHandover();
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
    if (pause) {
        cancelHandover();
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

void SoundPlayer::setRepeat(bool on)
{
    bRepeat = on;
}

bool SoundPlayer::isCurrentSampleLooping() const
{
    if (player.empty() || curSound < 0 || curSound >= static_cast<int>(player.size())) {
        return false;
    }
    return player[static_cast<size_t>(curSound)]->isLoopOn();
}

void SoundPlayer::setCurrentSampleLooping(bool on)
{
    if (player.empty() || curSound < 0 || curSound >= static_cast<int>(player.size())) {
        return;
    }
    if (on) {
        cancelHandover();
    }
    // Switching off lets the current pass finish at the end point; the pad then moves on.
    player[static_cast<size_t>(curSound)]->setLoopOn(on);
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
    cancelHandover();
    if (bPlayingDelay) {
        player[curSound]->curDelay = player[curSound]->totalDelay * (1.0f - pct);
    } else if (player[curSound]->audioPlayer) {
        player[curSound]->audioPlayer->seekTo(pct);
    }
}

void SoundPlayer::seekSample(int index, float pct)
{
    if (index < 0 || index >= static_cast<int>(player.size())) {
        return;
    }
    cancelHandover();
    if (OpenALSoundPlayer* audio = player[static_cast<size_t>(index)]->audioPlayer) {
        audio->seekTo(pct);
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

bool SoundPlayer::isRepeating() const
{
    return bRepeat;
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
    applyRandomPan(curSound);
}

void SoundPlayer::applyRandomPan(int index)
{
    if (!bRandomPan || index < 0 || index >= static_cast<int>(player.size())) {
        return;
    }
    AudioSample* sample = player[static_cast<size_t>(index)];
    if (!sample->audioPlayer->canPan()) {
        return;
    }
    sample->setPan(randomF());
    emit panRandomised(index, sample->getPan());
}

void SoundPlayer::tryArmHandover()
{
    // Only when the next sample follows straight on: no delay between samples, and the
    // current sample is heading for its end rather than looping.
    if (handover.armed || bPaused || bPlayingDelay || player.empty() || minDelay > 0 || maxDelay > 0) {
        return;
    }
    if (!OpenALSoundPlayer::scheduledStartAvailable()) {
        return;
    }
    const int count = static_cast<int>(player.size());
    if (curSound < 0 || curSound >= count) {
        return;
    }
    AudioSample* current = player[static_cast<size_t>(curSound)];
    if (!current->audioPlayer->isLoaded() || current->isLoopOn()) {
        return;
    }
    int64_t endNs = 0;
    if (!current->audioPlayer->predictEndDeviceTime(endNs)) {
        return;
    }
    if (endNs - OpenALSoundPlayer::deviceClockNs() > kHandoverArmNs) {
        return;
    }
    const int next = chooseNext();
    if (next < 0 || next >= count || next == curSound) {
        return; // the end of the list, or a single sample repeating (that one loops instead)
    }
    AudioSample* following = player[static_cast<size_t>(next)];
    if (!following->audioPlayer->isLoaded() || following->audioPlayer->isPlayingOut()) {
        return; // still playing the end of its last run; try again next tick
    }
    following->totalDelay = 0.0f;
    following->curDelay = 0.0f;
    following->audioPlayer->stop(); // at its begin frame, queue filled
    applyRandomPan(next);
    if (baseVolume >= 0.0f) {
        following->audioPlayer->setVolume(baseVolume * following->getGain());
    }
    if (!following->audioPlayer->playAtDeviceTime(endNs)) {
        return;
    }
    handover.armed = true;
    handover.next = next;
    handover.from = curSound;
    handover.endNs = endNs;
}

void SoundPlayer::checkHandover()
{
    if (!handover.armed) {
        return;
    }
    const int count = static_cast<int>(player.size());
    if (handover.from != curSound || handover.from < 0 || handover.from >= count || handover.next < 0 || handover.next >= count) {
        cancelHandover();
        return;
    }
    if (OpenALSoundPlayer::deviceClockNs() >= handover.endNs) {
        return; // already started: the end notice will make it current
    }
    AudioSample* current = player[static_cast<size_t>(curSound)];
    // The pad's own rules may have changed: Repeat switched off before the last sample
    // ends, a delay added, or the order changed.
    int expected = handover.next;
    if (curSound >= count - 1) {
        expected = bRepeat ? 0 : -1;
    } else if (!bRandomPlayback) {
        expected = curSound + 1;
    }
    int64_t endNs = 0;
    const bool stillValid = expected == handover.next && minDelay <= 0 && maxDelay <= 0
        && !bPaused && !current->isLoopOn()
        && current->audioPlayer->predictEndDeviceTime(endNs)
        && std::llabs(endNs - handover.endNs) <= kHandoverDriftNs;
    if (!stillValid) {
        // Seeked, looped, re-pitched or paused: withdraw it; the next tick schedules again if it applies.
        cancelHandover();
    }
}

void SoundPlayer::cancelHandover()
{
    if (!handover.armed) {
        return;
    }
    const int next = handover.next;
    handover = Handover{};
    if (next >= 0 && next < static_cast<int>(player.size())) {
        player[static_cast<size_t>(next)]->audioPlayer->stop();
    }
}

void SoundPlayer::onPlaybackEnded(OpenALSoundPlayer* ended)
{
    // Called on the stream thread: only queue it.
    std::lock_guard<std::mutex> lock(endedMutex);
    endedQueue.push_back(ended);
}

int SoundPlayer::indexOf(const OpenALSoundPlayer* audio) const
{
    for (int i = 0; i < static_cast<int>(player.size()); ++i) {
        if (player[static_cast<size_t>(i)]->audioPlayer == audio) {
            return i;
        }
    }
    return -1;
}

void SoundPlayer::handleEnded(OpenALSoundPlayer* audio)
{
    const int index = indexOf(audio);
    if (index < 0 || !audio->hasEnded()) {
        return; // not ours, or withdrawn (seeked or restarted since)
    }
    if (handover.armed && index == handover.next && index != curSound) {
        // A very short next sample can finish rendering before it's even heard.
        heldEnded.push_back(audio);
        return;
    }
    if (index != curSound) {
        return; // a sample that is no longer current (e.g. still playing out its tail)
    }
    if (bPaused) {
        heldEnded.push_back(audio); // acted on once the pad resumes
        return;
    }
    if (handover.armed && handover.from == curSound) {
        // The next sample is already scheduled on the device: just make it current.
        curSound = handover.next;
        handover = Handover{};
        bStartFromBeginning = false; // its random pan was applied when it was scheduled
        bPlayingDelay = false;
        bPaused = false;
        bCheckPlayBackEnded = true;
        applyVolumeToCurrent();
        return;
    }
    advanceAfterEnd();
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
