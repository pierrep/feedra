#pragma once

// The one OpenAL device and context, and everything shared by every voice: the effect buses
// (two EAX reverbs, two convolution reverbs), scheduled starts and the device clock, and the
// extension entry points. Opened once at startup (MainWindow) and closed once at shutdown
// (main.cpp). Use from the UI thread, except where noted.

#include <cstdint>
#include <filesystem>
#include <string>

#if defined(__APPLE__) && !defined(FEEDRA_OPENAL_SOFT)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

namespace AudioDevice {

// Effect buses and sends: 0 and 1 are the EAX reverbs, 2 and 3 the convolution reverbs.
// Every source has one aux send per bus, numbered the same.
constexpr int kEaxReverbCount = 2;
constexpr int kConvolutionCount = 2;
constexpr int kSendCount = kEaxReverbCount + kConvolutionCount;

bool initialize();
void close();
bool isOpen();
// Called once per UI tick.
void process();

// The system's default output, and moving the open device to another (ALC_SOFT_reopen_device).
// reopen returns 0 on success, -1 without the extension, -2 with no device, -3 if it failed.
std::string defaultDeviceName();
int reopen(const char* deviceName);

// Float buffers (AL_EXT_FLOAT32). Any thread.
bool floatOutput();
// Stereo panned by turning its channels (AL_EXT_STEREO_ANGLES).
bool stereoAngles();

// Scheduled starts (AL_SOFT_source_start_delay + ALC_SOFT_device_clock).
bool scheduledStartAvailable();
int64_t clockNs();
// Starts `count` sources so their first frame is mixed at `startNs` on the device clock.
// Returns false if it couldn't be scheduled (the caller then starts them now).
bool playAtTime(int count, const ALuint* sources, int64_t startNs);

// Source queries from AL_SOFT_source_latency. Any thread that may use OpenAL.
// Offset in seconds into the queue and the output latency in seconds.
bool offsetLatency(ALuint source, double& offsetSec, double& latencySec);
// Offset in 32.32 fixed-point frames, and the device clock when it was taken.
bool sampleOffsetClock(ALuint source, int64_t& offsetFixed, int64_t& clockNsOut);

// Effect sends.
bool sendAvailable(int bus);
ALuint effectSlot(int bus);
// Low-pass filters, used as each send's level.
ALuint newSendFilter();
void setSendFilterGain(ALuint filter, float gain);
void deleteFilter(ALuint& filter);

// EAX reverb presets.
int reverbPresetCount();
std::string reverbPresetId(int index);
std::string reverbPresetLabel(int index);
int reverbPresetIndex(int which = 0);
void setReverbPreset(int index, int which = 0);
bool setReverbPresetById(const std::string& id, int which = 0);

// Convolution reverbs.
float defaultConvolutionGain();
float convolutionGain(int which = 0);
void setConvolutionGain(float gain, int which = 0);
bool convolutionAvailable();
std::filesystem::path convolutionImpulsePath(int which = 0);
bool setConvolutionImpulse(const std::filesystem::path& path, int which = 0);

// AL error names, for logs.
std::string alErrorString(ALenum error);

} // namespace AudioDevice
