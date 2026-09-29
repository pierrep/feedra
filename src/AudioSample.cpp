#include "AudioSample.h"

AudioSample::AudioSample() = default;

AudioSample::~AudioSample()
{
    delete audioPlayer;
    audioPlayer = nullptr;
}

void AudioSample::setPitch(float val)
{
    pitch = val;
    if (audioPlayer) {
        audioPlayer->setSpeed(val);
    }
}

void AudioSample::setGain(float val)
{
    gain = val;
}

void AudioSample::setPan(float val)
{
    if (audioPlayer) {
        audioPlayer->setPan(val);
    }
}

float AudioSample::getPan() const
{
    return audioPlayer ? audioPlayer->getPan() : 0.0f;
}

bool AudioSample::isSpatialisedStereo() const
{
    return audioPlayer ? audioPlayer->isSpatialisedStereo() : false;
}

void AudioSample::setLoopRegion(const LoopRegion& region)
{
    if (audioPlayer) {
        audioPlayer->setLoopRegion(region);
    }
}

LoopRegion AudioSample::loopRegion() const
{
    return audioPlayer ? audioPlayer->getLoopRegion() : LoopRegion{};
}

void AudioSample::setLoopOn(bool on)
{
    LoopRegion region = loopRegion();
    region.loop = on;
    setLoopRegion(region);
}

void AudioSample::setWidth(float width)
{
    if (audioPlayer) {
        audioPlayer->setStereoWidth(width);
    }
}

float AudioSample::getWidth() const
{
    return audioPlayer ? audioPlayer->getStereoWidth() : 1.0f;
}
