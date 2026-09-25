#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <cstdint>

#if defined(__APPLE__) && !defined(FEEDRA_OPENAL_SOFT)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#include "kiss_fft.h"
#include "kiss_fftr.h"

#ifdef _WIN32
#include "sndfile.h"
#else
#include <sndfile.h>
#endif

#ifdef FEEDRA_USING_MPG123
typedef struct mpg123_handle_struct mpg123_handle;
#endif

enum FormatType {
    Int16,
    Float,
    IMA4,
    MSADPCM
};

struct DecodedChunk {
    std::vector<short> pcmShort;
    std::vector<float> pcmFloat;
};

struct DecodedAudio {
    bool ok = false;
    bool streaming = false;
    bool mp3 = false;
    bool streamEnded = false;
    std::filesystem::path path;
    std::string fileExtension;
    int fileFormat = 0;
    std::string formatString;
    std::string subformatString;
    FormatType sampleFormat = Int16;
    int channels = 0;
    int sampleRate = 0;
    float duration = 0.0f;
    double streamScale = 1.0;
    std::vector<short> pcmShort;
    std::vector<float> pcmFloat;
    std::vector<DecodedChunk> initialChunks;
    int64_t resumeFrames = 0;
    int64_t streamSamplesRead = 0;
    int mp3BufferSize = 0;
};

// Min/max envelope of a whole file, one pair per bin, for waveform displays.
struct WaveformPeaks {
    bool ok = false;
    std::vector<float> mins;
    std::vector<float> maxs;
};

void OpenALSoundUpdate();

class OpenALSoundPlayer {
public:
    using PlaybackEndedCallback = std::function<void(OpenALSoundPlayer*)>;

    OpenALSoundPlayer();
    ~OpenALSoundPlayer();

    static std::string getDefaultDeviceString();
    static ALCdevice* getCurrentDevice();
    static int reopenDevice(const char* devicename);
    static int listDevices(bool printOutput = true);
    static void printExtensions(const char *header, char separator, const char *extensions);

    bool load(const std::filesystem::path& fileName, bool stream = false);
    static DecodedAudio decodeFile(const std::filesystem::path& fileName, bool stream);
    bool uploadDecoded(DecodedAudio decoded);
    void unload();
    void play();
    void stop();
    void update();
    static void updateAll();

    void setVolume(float vol);
    void setPan(float vol);
    void setSpeed(float spd);
    void setPaused(bool bP);
    void setLoop(bool bLp);
    void setMultiPlay(bool bMp);
    void setPosition(float pct);
    void setPositionMS(int ms);
    // Jump to `pct` (0..1) of the file without ever blocking the stream thread.
    // While streaming, the request is handed to the stream thread, which drops the queued
    // audio and restarts from the new spot within a millisecond or two. When stopped or paused, the
    // queue is rebuilt at the new spot so the next play starts there.
    void seekTo(float pct);

    float getPosition() const;
    int getPositionMS() const;
    bool isPlaying() const;
    float getSpeed() const;
    float getPan() const;
    float getVolume() const;
    bool isPaused() const;
    bool isLoaded() const;
    bool isLooping() const;

    static void initialize();
    static void close();

    float * getSpectrum(int bands);
    static float * getSystemSpectrum(int bands);

    // Frame (0..total) the listener is hearing right now, compensating for queued stream
    // buffers and device latency. Returns -1 when unknown. Never takes the stream mutex.
    double getAudibleFrame() const;
    // getAudibleFrame() as 0..1 of the file, or -1 when unknown.
    float getAudiblePosition() const;
    int64_t getTotalFrames() const { return totalFrames; }
    const std::filesystem::path& getFilePath() const { return fileName; }

    // Decodes the whole file on the calling thread (use a background thread) and returns
    // `bins` min/max pairs. Independent of any player, so it never touches playback state.
    static WaveformPeaks computePeaks(const std::filesystem::path& fileName, int bins,
                                      const std::atomic<bool>* cancel = nullptr);

    float getDuration() const { return duration; }
    int getSampleRate() const { return samplerate; }
    int getNumChannels() const { return channels; }
    bool isStreamEnd() const { return stream_end; }
    float getReverbSend() const { return reverbSend; }
    void setReverbSend(float send) { reverbSend = send; }
    float getReverbSend2() const { return reverbSend2; }
    void setReverbSend2(float send) { reverbSend2 = send; }

    static int reverbPresetCount();
    static std::string reverbPresetId(int index);
    static std::string reverbPresetLabel(int index);
    static int reverbPresetIndex();
    static void setReverbPreset(int index);
    static bool setReverbPresetById(const std::string& id);

    static float defaultConvolutionGain();
    static float convolutionGain();
    static void setConvolutionGain(float gain);
    static bool convolutionAvailable();
    static std::filesystem::path convolutionImpulsePath();
    static bool setConvolutionImpulse(const std::filesystem::path& path);

    int getFileFormat() const { return fileformat; }
    ALenum getOpenALFormat() const { return openALformat; }
    std::string getFormatString() const { return format_string; }
    std::string getSubFormatString() const { return subformat_string; }

    int getNumSources() { return static_cast<int>(sources.size()); }
    bool isSpatialisedStereo() { return spatialisedStereo; }
    // Stereo files are split into one mono source per channel when spatialised.
    bool uploadDecoded(DecodedAudio decoded, bool spatialise);
    bool canPan() const { return channels == 1 || (channels == 2 && spatialisedStereo); }

    static void addPlaybackEndedListener(void* owner, PlaybackEndedCallback cb);
    static void removePlaybackEndedListener(void* owner);

    OpenALSoundPlayer* playerPtr = nullptr;

private:
    void threadedFunction();
    void startThread();
    void stopThread();
    void waitForThread();
    bool isThreadRunning() const;
    void sleepMs(int ms);

    void initFFT(int bands);
    float * getCurrentBufferSum(int size);

    static void createWindow(int size);
    static void runWindow(std::vector<float> & signal);
    static void initSystemFFT(int bands);
    static void notifyPlaybackEnded(OpenALSoundPlayer* player);

    // Refills the stream queue from the start of the file; the stream thread must not be running.
    bool primeStream(int64_t startFrame = 0);
    // Stops the sources, drops their queued buffers and refills them from `startFrame`.
    // Caller holds `mutex`. Leaves the sources stopped.
    bool rebuildQueueLocked(int64_t startFrame);
    // Moves the stream decoder; caller must own the stream (thread's lock or thread stopped).
    void seekDecoder(int64_t frame);
    void haltPlayback();
    bool attachDecodedStream(const DecodedAudio& decoded);
    void rebuildFftBuffers();

    // Stream queue tracking for getAudibleFrame(). The ring is only touched by whichever
    // thread is feeding the queue; the UI reads the published copy through a seqlock.
    int64_t decoderFramePosition() const;
    void resetQueueTracking();
    void pushQueuedChunk(int64_t start, int64_t frames);
    void popQueuedChunk();
    void publishQueue();
    void beginQueueChange() { queueSeq.fetch_add(1, std::memory_order_acq_rel); }
    void endQueueChange() { queueSeq.fetch_add(1, std::memory_order_release); }

    bool sfReadFile(const std::filesystem::path& path);
    bool sfStream(const std::filesystem::path& path);
#ifdef FEEDRA_USING_MPG123
    bool mpg123ReadFile(const std::filesystem::path& path);
    bool mpg123Stream(const std::filesystem::path& path);
#endif

    size_t readFile(const std::filesystem::path& fileName);
    size_t stream(const std::filesystem::path& fileName);

    std::thread worker;
    std::atomic<bool> threadRunning{false};
    mutable std::mutex mutex;

    bool isStreaming = true;
    bool bMultiPlay = false;
    bool bLoop = false;
    bool bLoadedOk = false;
    bool bPaused = false;
    float pan = 0.0f;
    float volume = 1.0f;
    float internalFreq = 44100.0f;
    float speed = 1.0f;
    unsigned int length = 0;

    static std::vector<float> window;
    static float windowSum;

    int channels = 0;
    float duration = 0.0f;
    int samplerate = 0;
    std::filesystem::path fileName;
    std::string file_extension;
    std::vector<ALuint> buffers;
    std::vector<ALuint> sources;

    std::vector<std::vector<float> > fftBuffers;
    kiss_fftr_cfg fftCfg = nullptr;
    std::vector<float> windowedSignal;
    std::vector<float> bins;
    std::vector<kiss_fft_cpx> cx_out;

    static kiss_fftr_cfg systemFftCfg;
    static std::vector<float> systemWindowedSignal;
    static std::vector<float> systemBins;
    static std::vector<kiss_fft_cpx> systemCx_out;

    SNDFILE* streamf = nullptr;
    ALint byteblockalign = 0;
    ALint splblockalign = 0;
    size_t stream_samples_read = 0;
#ifdef FEEDRA_USING_MPG123
    mpg123_handle * mp3streamf = nullptr;
    int stream_encoding = 0;
#endif
    int mp3_buffer_size = 0;
    int fileformat = 0;
    std::string format_string;
    std::string subformat_string;
    ALenum openALformat = AL_NONE;
    enum FormatType sample_format = Int16;
    double stream_scale = 1.0;
    std::vector<short> buffer_short;
    std::vector<float> buffer_float;

    std::atomic<bool> stream_end{false};
    bool streamPrimed = false;

    bool spatialisedStereo = false;

    int64_t totalFrames = 0;
    struct QueuedChunk { int64_t start = 0; int64_t frames = 0; };
    static constexpr int kQueueRing = 8;
    QueuedChunk queuedChunks[kQueueRing];
    int queuedHead = 0;
    int queuedCount = 0;
    // Start frame of the current run (since the last load, prime or seek) and how many frames
    // of it have been unqueued, so the latency correction never reaches back past the run start.
    bool runPending = true;
    int64_t runStartFrame = 0;
    int64_t runUnqueued = 0;
    std::atomic<uint32_t> queueSeq{0};
    std::atomic<int64_t> pubStart0{0};
    std::atomic<int64_t> pubFrames0{0};
    std::atomic<int64_t> pubStart1{0};
    std::atomic<int> pubCount{0};
    std::atomic<int64_t> pubRunStart{0};
    std::atomic<int64_t> pubRunUnqueued{0};
    std::atomic<int64_t> pendingSeekFrame{-1};

    ALuint filters[2] = { 0, 0 };
    float reverbSend = 0.0f;
    float reverbSend2 = 0.0f;
    bool bUseFilter = false;
};
