#include "CueOutput.h"

#include "AudioCodec.h"

#include <QDebug>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using SetThreadContextFn = ALCboolean(ALC_APIENTRY*)(ALCcontext*);

constexpr int kBuffers = 4;
constexpr int64_t kBufferFrames = 8192; // ~190 ms at 44.1 kHz: four of them ride out a busy disk

enum class Command { None, Play, Stop, Seek };

struct State {
    std::string deviceName;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    bool quit = false;

    // Requests from the UI thread, read and cleared by the cue thread.
    Command command = Command::None;
    std::filesystem::path path;
    uint64_t request = 0;
    float seekPct = 0.0f;
    std::atomic<float> volume { 1.0f };

    // Written by the cue thread.
    CueOutput::Status status;
};

std::unique_ptr<State> g_state;

void setStatus(State& st, const CueOutput::Status& status)
{
    std::lock_guard<std::mutex> lock(st.mutex);
    st.status = status;
}

// Everything the cue thread owns. Only ever touched on that thread, with its context current.
class CueVoice
{
public:
    explicit CueVoice(bool floatFormat)
        : m_float(floatFormat)
    {
        alGenSources(1, &m_source);
        alSourcei(m_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(m_source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alGenBuffers(kBuffers, m_buffers);
    }

    ~CueVoice()
    {
        halt();
        alDeleteSources(1, &m_source);
        alDeleteBuffers(kBuffers, m_buffers);
    }

    // Opens and starts a file. False if it can't be played.
    bool start(const std::filesystem::path& path)
    {
        halt();
        m_reader.close();
        if (!m_reader.open(path, m_float, true) || m_reader.channels() < 1 || m_reader.channels() > 2) {
            m_reader.close();
            return false;
        }
        m_rate = m_reader.sampleRate();
        m_total = m_reader.totalFrames();
        m_duration = m_reader.duration();
        return fillAndPlay(0);
    }

    void seek(float pct)
    {
        if (!m_reader.isOpen() || m_total <= 0) {
            return;
        }
        halt();
        const auto target = static_cast<int64_t>(std::clamp(pct, 0.0f, 1.0f) * float(m_total));
        fillAndPlay(m_reader.seek(std::min(target, m_total - 1)));
    }

    void stop()
    {
        halt();
        m_reader.close();
        m_ended = false;
    }

    // Refills played buffers. Call every few milliseconds.
    void service()
    {
        if (!m_reader.isOpen()) {
            return;
        }
        ALint processed = 0;
        alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(m_source, 1, &buffer);
            if (!m_queued.empty()) {
                m_playedFrames += m_queued.front();
                m_queued.pop_front();
            }
            if (!m_eof) {
                queue(buffer);
            }
        }
        ALint state = AL_STOPPED;
        alGetSourcei(m_source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            if (!m_queued.empty()) {
                alSourcePlay(m_source); // ran dry before a refill (a slow disk): carry on
            } else if (m_eof) {
                m_ended = true;
                m_reader.close();
            }
        }
    }

    void setGain(float gain)
    {
        if (gain != m_gain) {
            m_gain = gain;
            alSourcef(m_source, AL_GAIN, gain);
        }
    }

    bool isPlaying() const { return m_reader.isOpen() && !m_ended; }
    bool ended() const { return m_ended; }
    double duration() const { return m_duration; }

    float position() const
    {
        if (m_total <= 0) {
            return 0.0f;
        }
        ALint offset = 0;
        alGetSourcei(m_source, AL_SAMPLE_OFFSET, &offset);
        const int64_t frame = m_ended ? m_total : m_playedFrames + offset;
        return std::clamp(float(frame) / float(m_total), 0.0f, 1.0f);
    }

private:
    // Stops the source and takes every buffer back.
    void halt()
    {
        alSourceStop(m_source);
        alSourcei(m_source, AL_BUFFER, 0);
        m_queued.clear();
    }

    bool fillAndPlay(int64_t fromFrame)
    {
        m_playedFrames = fromFrame;
        m_eof = false;
        m_ended = false;
        for (ALuint buffer : m_buffers) {
            if (m_eof) {
                break;
            }
            queue(buffer);
        }
        if (m_queued.empty()) {
            m_ended = true;
            return false;
        }
        alSourcePlay(m_source);
        return alGetError() == AL_NO_ERROR;
    }

    // Reads the next block into `buffer` and queues it. Sets m_eof at the end of the file.
    void queue(ALuint buffer)
    {
        const int channels = m_reader.channels();
        m_floats.resize(static_cast<size_t>(kBufferFrames) * static_cast<size_t>(channels));
        const int64_t got = m_reader.readFloat(m_floats.data(), kBufferFrames, m_scratch);
        if (got < kBufferFrames) {
            m_eof = true;
        }
        if (got <= 0) {
            return;
        }
        const size_t samples = static_cast<size_t>(got) * static_cast<size_t>(channels);
        if (m_float) {
            alBufferData(buffer, channels == 2 ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_MONO_FLOAT32,
                m_floats.data(), static_cast<ALsizei>(samples * sizeof(float)), m_rate);
        } else {
            m_shorts.resize(samples);
            for (size_t i = 0; i < samples; ++i) {
                m_shorts[i] = AudioCodec::floatToShort(m_floats[i]);
            }
            alBufferData(buffer, channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16,
                m_shorts.data(), static_cast<ALsizei>(samples * sizeof(short)), m_rate);
        }
        alSourceQueueBuffers(m_source, 1, &buffer);
        m_queued.push_back(got);
    }

    bool m_float = false;
    ALuint m_source = 0;
    ALuint m_buffers[kBuffers] = {};
    AudioCodec::FileReader m_reader;
    std::vector<float> m_floats;
    std::vector<short> m_shorts;
    std::vector<short> m_scratch;
    std::deque<int64_t> m_queued; // frames in each queued buffer, oldest first
    int64_t m_playedFrames = 0;   // frames before the oldest queued buffer
    int64_t m_total = 0;
    int m_rate = 44100;
    double m_duration = 0.0;
    float m_gain = 1.0f;
    bool m_eof = false;
    bool m_ended = false;
};

void run(State* st, std::promise<bool> opened)
{
    auto setThreadContext = reinterpret_cast<SetThreadContextFn>(alcGetProcAddress(nullptr, "alcSetThreadContext"));
    ALCdevice* device = alcOpenDevice(st->deviceName.c_str());
    ALCcontext* context = device ? alcCreateContext(device, nullptr) : nullptr;
    if (!context || !setThreadContext || !setThreadContext(context)) {
        qWarning() << "Cue output: couldn't open" << QString::fromStdString(st->deviceName);
        if (context) {
            alcDestroyContext(context);
        }
        if (device) {
            alcCloseDevice(device);
        }
        opened.set_value(false);
        return;
    }
    qInfo() << "Cue output on" << QString::fromStdString(st->deviceName);
    opened.set_value(true);

    {
        CueVoice voice(alIsExtensionPresent("AL_EXT_FLOAT32") == AL_TRUE);
        CueOutput::Status status;
        for (;;) {
            Command command = Command::None;
            std::filesystem::path path;
            uint64_t request = 0;
            float seekPct = 0.0f;
            {
                std::unique_lock<std::mutex> lock(st->mutex);
                st->wake.wait_for(lock, std::chrono::milliseconds(10),
                    [st]() { return st->quit || st->command != Command::None; });
                if (st->quit) {
                    break;
                }
                command = st->command;
                st->command = Command::None;
                path = st->path;
                request = st->request;
                seekPct = st->seekPct;
            }
            voice.setGain(st->volume.load());
            switch (command) {
            case Command::Play:
                status = CueOutput::Status();
                status.request = request;
                status.failed = !voice.start(path);
                status.duration = voice.duration();
                break;
            case Command::Stop:
                voice.stop();
                status.playing = false;
                break;
            case Command::Seek:
                voice.seek(seekPct);
                break;
            case Command::None:
                break;
            }
            voice.service();
            status.playing = voice.isPlaying();
            status.ended = voice.ended();
            status.position = voice.position();
            setStatus(*st, status);
        }
    }
    setThreadContext(nullptr);
    alcDestroyContext(context);
    alcCloseDevice(device);
}

} // namespace

bool CueOutput::available()
{
    return alcIsExtensionPresent(nullptr, "ALC_EXT_thread_local_context") == ALC_TRUE;
}

bool CueOutput::open(const std::string& deviceName)
{
    close();
    if (deviceName.empty() || !available()) {
        return false;
    }
    auto st = std::make_unique<State>();
    st->deviceName = deviceName;
    std::promise<bool> opened;
    std::future<bool> result = opened.get_future();
    st->thread = std::thread(run, st.get(), std::move(opened));
    if (!result.get()) {
        st->thread.join();
        return false;
    }
    g_state = std::move(st);
    return true;
}

void CueOutput::close()
{
    if (!g_state) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_state->mutex);
        g_state->quit = true;
    }
    g_state->wake.notify_all();
    g_state->thread.join();
    g_state.reset();
}

bool CueOutput::isOpen()
{
    return g_state != nullptr;
}

std::string CueOutput::deviceName()
{
    return g_state ? g_state->deviceName : std::string();
}

uint64_t CueOutput::play(const std::filesystem::path& path)
{
    if (!g_state) {
        return 0;
    }
    uint64_t request = 0;
    {
        std::lock_guard<std::mutex> lock(g_state->mutex);
        g_state->command = Command::Play;
        g_state->path = path;
        request = ++g_state->request;
    }
    g_state->wake.notify_all();
    return request;
}

void CueOutput::stop()
{
    if (!g_state) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_state->mutex);
        g_state->command = Command::Stop;
    }
    g_state->wake.notify_all();
}

void CueOutput::seek(float pct)
{
    if (!g_state) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_state->mutex);
        // A seek doesn't replace a play that hasn't been taken yet.
        if (g_state->command != Command::Play) {
            g_state->command = Command::Seek;
        }
        g_state->seekPct = pct;
    }
    g_state->wake.notify_all();
}

void CueOutput::setVolume(float gain)
{
    if (g_state) {
        g_state->volume.store(gain);
    }
}

CueOutput::Status CueOutput::status()
{
    if (!g_state) {
        return Status();
    }
    std::lock_guard<std::mutex> lock(g_state->mutex);
    return g_state->status;
}
