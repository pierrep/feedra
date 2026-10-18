#pragma once

// A second output for the Files tab's previews ("cue"), so the table doesn't hear you browsing.
//
// It's a separate OpenAL device with its own context, used only from its own thread through
// ALC_EXT_thread_local_context: the main context, the voices and the stream service never
// see it. One file plays at a time. Every call here is from the UI thread and only posts a
// request or reads the last status, so none of them wait on the device.

#include <cstdint>
#include <filesystem>
#include <string>

namespace CueOutput {

// The extension the cue thread needs. Without it, previews stay on the main output.
bool available();

// Opens `deviceName` (a name from AudioDevice::outputDevices()) as the cue output, closing any
// other. Waits for the device to open; false if it couldn't (previews then use the main output).
bool open(const std::string& deviceName);
void close();
bool isOpen();
std::string deviceName();

// Starts `path` from the beginning, replacing whatever was playing. Returns the request's
// number, which status() reports once the cue thread has taken it.
uint64_t play(const std::filesystem::path& path);
void stop();
// 0..1 of the file.
void seek(float pct);
// Linear gain.
void setVolume(float gain);

struct Status {
    uint64_t request = 0; // the play() this is about
    bool playing = false; // started and not yet at the end
    bool failed = false;  // the file couldn't be opened or played
    bool ended = false;   // played to the end
    float position = 0.0f; // 0..1, what's being heard
    double duration = 0.0;
};
Status status();

} // namespace CueOutput
