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
    // Exact length in frames when the decoder knows it (0 when unknown).
    int64_t totalFrames = 0;
    int mp3BufferSize = 0;
};

// Min/max envelope of a whole file, one pair per bin, for waveform displays.
struct WaveformPeaks {
    bool ok = false;
    std::vector<float> mins;
    std::vector<float> maxs;
};

// Which part of a file plays, and how. Times are in seconds so a region survives
// reloads and does not depend on the file's sample rate.
struct LoopRegion {
    double start = 0.0;        // S: loop start
    double end = -1.0;         // E: loop end and where playback stops; <= 0 means the end of the file
    bool loop = false;         // loop S..E seamlessly
    bool playFromStart = true; // begin at 0:00 (anything before S is an intro) or at S
    bool playToEnd = true;     // when not looping, play on past E to the end of the file (an outro) or stop at E
    // Crossfade at the loop join, and fade-out at a moved end point. The player also keeps
    // it within half the loop.
    double crossfadeMs = 10.0;
    static constexpr double kMaxCrossfadeMs = 10000.0;
    bool operator==(const LoopRegion& o) const
    {
        return start == o.start && end == o.end && loop == o.loop && playFromStart == o.playFromStart
            && playToEnd == o.playToEnd && crossfadeMs == o.crossfadeMs;
    }
    bool operator!=(const LoopRegion& o) const { return !(*this == o); }
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

    // Loop region and loop switch. Safe to call at any time, including before loading and
    // while playing: the stream thread picks the change up at its next chunk (a few ms)
    // without the caller waiting on it. When stopped, the queue is refilled at the new begin.
    void setLoopRegion(const LoopRegion& region);
    LoopRegion getLoopRegion() const;
    // Frame playback begins at: 0, or S when "play from start" is off.
    int64_t getBeginFrame() const;

    // Scheduled starts (AL_SOFT_source_start_delay + ALC_SOFT_device_clock).
    static bool scheduledStartAvailable();
    static int64_t deviceClockNs();
    // Device-clock time at which the last frame of this sample will be mixed, if it is
    // playing towards a known end (not looping, no settings change pending).
    bool predictEndDeviceTime(int64_t& endNs) const;
    // Starts playback from the begin frame so the first frame is mixed at `startNs`.
    bool playAtDeviceTime(int64_t startNs);
    // Reached its end (thread finished) but the source is still playing the last of the queue.
    bool isPlayingOut() const;
    // True once this run has reached its end and said so. A seek or restart afterwards
    // (e.g. from the Waveform tab) makes it false again, withdrawing that notice.
    bool hasEnded() const;
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

    // Refills the stream queue from `startFrame` (-1: the begin frame); the stream thread must not be running.
    bool primeStream(int64_t startFrame = -1);
    // Stops the sources, drops their queued buffers and refills them from `startFrame`.
    // Caller holds `mutex`. Leaves the sources stopped.
    bool rebuildQueueLocked(int64_t startFrame, bool isSeek);
    // Moves the decoder; caller must own it (thread's lock or thread stopped).
    void seekDecoder(int64_t frame);
    void haltPlayback();
    bool attachDecodedStream(const DecodedAudio& decoded);
    void rebuildFftBuffers();

    // --- Region renderer (runs wherever the queue is filled, under `mutex`) ---
    struct Segment { int64_t out = 0; int64_t file = 0; int64_t frames = 0; };
    static constexpr int kMaxSegments = 8;
    struct ChunkRecord {
        int64_t outStart = 0;   // output frames rendered in this run before this chunk
        int64_t frames = 0;
        int segCount = 0;
        Segment seg[kMaxSegments];
        int64_t toEnd = -1;     // frames still to render until the end after this chunk; -1 = none/unknown
    };
    int chunkFrames() const;
    int64_t readFrames(float* dst, int64_t frames);
    void applyPendingRegion();
    void computeEffectiveRegion();
    void ensureHeadCache();
    void addSegment(ChunkRecord& rec, int64_t out, int64_t file, int64_t frames);
    void applyFadeIn(float* data, int64_t frames);
    int renderChunk(ChunkRecord& rec);
    void convertRendered(int frames);
    bool fillBuffer(ALuint buffer, int channelIndex);
    void refillSparesLocked();
    // History of rendered chunks for the playhead. Guarded by historyMutex, which is only
    // held around queue changes and position reads, never while decoding.
    void resetHistoryLocked();
    void pushHistoryLocked(const ChunkRecord& rec);
    double mapOutputFrameLocked(double outFrame) const;

    bool sfReadFile(const std::filesystem::path& path);
#ifdef FEEDRA_USING_MPG123
    bool mpg123ReadFile(const std::filesystem::path& path);
#endif

    size_t readFile(const std::filesystem::path& fileName);

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

    // Region settings: written by the UI under regionMutex, applied by the renderer.
    mutable std::mutex regionMutex;
    LoopRegion requestedRegion;
    std::atomic<uint32_t> regionVersion{1};
    std::atomic<uint32_t> appliedRegionVersion{0};
    LoopRegion activeRegion;
    int64_t regS = 0, regE = 0;  // effective region in frames
    int64_t xfFrames = 0;        // effective loop crossfade length
    int64_t edgeFadeFrames = 0;  // short fade-in at a mid-file start, fade-out at a moved end
    bool regLoop = false;
    bool regFromStart = true;
    bool regToEnd = true;
    // Where playback stops when it isn't looping: E, or the end of the file with "play to end".
    int64_t stopFrame() const;
    std::vector<float> headCache; // [S, S + xf), interleaved
    bool headCacheValid = false;

    // Decoder source and position.
    bool memoryBacked = false;
    std::vector<short> memShort;
    std::vector<float> memFloat;
    int64_t memFrames = 0;
    std::atomic<int64_t> decPos{0};
    std::vector<short> readScratch;
    std::vector<float> work;
    std::vector<float> scratchFloat;
    std::vector<short> scratchShort;

    // Renderer state.
    bool renderEnded = false;
    std::atomic<bool> endNotified{false}; // "playback ended" already sent for this run
    std::vector<ALuint> spareBuffers;     // unqueued after the end; see refillSparesLocked()
    int64_t xfPos = -1;          // position inside a loop crossfade, -1 when not in one
    int64_t fadeInLen = 0, fadeInDone = 0;
    int64_t outCounter = 0;

    mutable std::mutex historyMutex;
    static constexpr int kHistory = 12;
    ChunkRecord history[kHistory];
    int historyCount = 0;        // records kept (queued + recently played)
    int historyNewest = -1;      // index of the newest record
    int queuedRecords = 0;       // how many of the newest records are still in the AL queue
    bool historyEnded = false;   // the newest record is the last of the run
    int64_t lastFileFrame = 0;   // file frame just after the last rendered frame
    std::atomic<int64_t> pendingStartNs{0};

    std::atomic<int64_t> pendingSeekFrame{-1};

    ALuint filters[2] = { 0, 0 };
    float reverbSend = 0.0f;
    float reverbSend2 = 0.0f;
    bool bUseFilter = false;
};
