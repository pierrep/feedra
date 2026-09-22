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
