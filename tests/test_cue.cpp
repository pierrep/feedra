// The preview (cue) output: a second OpenAL device on its own thread. Plays, seeks, ends,
// fails cleanly on a bad file, and closes while playing. The main device stays usable
// throughout, and the device lists both use is checked too.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null), which mixes in real time.

#include "AudioDevice.h"
#include "CueOutput.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

#include <sndfile.h>

namespace {

int g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);        \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

void writeWav(const std::filesystem::path& path, double secs, int channels)
{
    SF_INFO info{};
    info.samplerate = 44100;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(path.string().c_str(), SFM_WRITE, &info);
    std::vector<float> buf(static_cast<size_t>(secs * 44100) * static_cast<size_t>(channels), 0.1f);
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size() / static_cast<size_t>(channels)));
    sf_close(f);
}

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Waits until the cue thread has taken `request` and `pred` holds.
template <typename Pred>
CueOutput::Status waitFor(uint64_t request, Pred pred, int timeoutMs = 3000)
{
    CueOutput::Status st;
    for (int waited = 0; waited < timeoutMs; waited += 5) {
        st = CueOutput::status();
        if (st.request == request && pred(st)) {
            return st;
        }
        sleepMs(5);
    }
    return st;
}

} // namespace

int main()
{
    if (!AudioDevice::initialize()) {
        std::printf("no audio device: skipped\n");
        return 77;
    }
    const std::vector<std::string> devices = AudioDevice::outputDevices();
    CHECK(!devices.empty(), "no output devices listed");
    CHECK(!AudioDevice::currentDeviceName().empty(), "no current device name");
    CHECK(AudioDevice::isConnected(), "main device not connected");
    if (!CueOutput::available()) {
        std::printf("no ALC_EXT_thread_local_context: skipped\n");
        AudioDevice::close();
        return 77;
    }

    const auto dir = std::filesystem::temp_directory_path() / "feedra-cue-test";
    std::filesystem::create_directories(dir);
    const auto longFile = dir / "long.wav";
    const auto shortFile = dir / "short.wav";
    const auto monoFile = dir / "mono.wav";
    writeWav(longFile, 4.0, 2);
    writeWav(shortFile, 0.3, 2);
    writeWav(monoFile, 1.0, 1);

    std::printf("open\n");
    CHECK(!CueOutput::open("no such device, surely"), "a missing device opened");
    CHECK(!CueOutput::isOpen(), "open after a failure");
    CHECK(CueOutput::open(devices.front()), "couldn't open %s", devices.front().c_str());
    CHECK(CueOutput::deviceName() == devices.front(), "device name");

    std::printf("play, position, seek\n");
    uint64_t req = CueOutput::play(longFile);
    CueOutput::Status st = waitFor(req, [](const CueOutput::Status& s) { return s.playing; });
    CHECK(st.playing && !st.failed, "long file not playing");
    CHECK(std::fabs(st.duration - 4.0) < 0.01, "duration %f", st.duration);
    sleepMs(500);
    st = CueOutput::status();
    CHECK(st.position > 0.05f && st.position < 0.3f, "position after 0.5 s: %f", st.position);
    CueOutput::seek(0.75f);
    st = waitFor(req, [](const CueOutput::Status& s) { return s.position >= 0.74f; });
    CHECK(st.position >= 0.74f && st.position < 0.9f, "position after seek: %f", st.position);
    CueOutput::setVolume(0.5f);

    std::printf("a new file replaces it\n");
    req = CueOutput::play(monoFile);
    st = waitFor(req, [](const CueOutput::Status& s) { return s.playing; });
    CHECK(st.playing && std::fabs(st.duration - 1.0) < 0.01, "mono file: playing %d duration %f", st.playing, st.duration);
    CHECK(st.position < 0.5f, "mono starts at %f", st.position);

    std::printf("play to the end\n");
    req = CueOutput::play(shortFile);
    st = waitFor(req, [](const CueOutput::Status& s) { return s.ended; }, 4000);
    CHECK(st.ended && !st.playing, "short file didn't end: ended %d playing %d", st.ended, st.playing);
    CHECK(st.position > 0.99f, "end position %f", st.position);

    std::printf("a bad file\n");
    req = CueOutput::play(dir / "missing.wav");
    st = waitFor(req, [](const CueOutput::Status& s) { return s.failed; });
    CHECK(st.failed && !st.playing, "missing file didn't fail");

    std::printf("stop\n");
    req = CueOutput::play(longFile);
    waitFor(req, [](const CueOutput::Status& s) { return s.playing; });
    CueOutput::stop();
    st = waitFor(req, [](const CueOutput::Status& s) { return !s.playing; });
    CHECK(!st.playing, "still playing after stop");

    std::printf("close while playing, then the main device again\n");
    req = CueOutput::play(longFile);
    waitFor(req, [](const CueOutput::Status& s) { return s.playing; });
    CueOutput::close();
    CHECK(!CueOutput::isOpen(), "still open after close");
    CHECK(CueOutput::play(longFile) == 0, "play with no cue output");
    CHECK(AudioDevice::isOpen() && AudioDevice::isConnected(), "main device lost");
    for (int i = 0; i < 3; ++i) {
        CHECK(CueOutput::open(devices.front()), "reopen %d", i);
        CueOutput::close();
    }

    AudioDevice::close();
    std::filesystem::remove_all(dir);
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
