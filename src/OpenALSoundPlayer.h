#pragma once

// One playing sample: an OpenAL source fed through a short queue of buffers.
//
// The pieces it's made of live in their own files:
//   AudioDevice     the OpenAL device, effect buses, scheduled starts (shared by every voice)
//   AudioCodec      reading files (libsndfile, mpg123)
//   RegionRenderer  the loop region, crossfades and fades, as plain computation
// What's left here is the voice itself: the source and its queue, the stream service that
// keeps playing queues topped up, the playhead, and the voice budget.

#include "AudioCodec.h"
#include "AudioDevice.h"
#include "RegionRenderer.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class OpenALSoundPlayer {
public:
    using PlaybackEndedCallback = std::function<void(OpenALSoundPlayer*)>;

    static constexpr int kEaxReverbCount = AudioDevice::kEaxReverbCount;
    static constexpr int kConvolutionCount = AudioDevice::kConvolutionCount;
    static constexpr int kSendCount = AudioDevice::kSendCount;

    OpenALSoundPlayer();
    ~OpenALSoundPlayer();
    OpenALSoundPlayer(const OpenALSoundPlayer&) = delete;
    OpenALSoundPlayer& operator=(const OpenALSoundPlayer&) = delete;

    // Decodes on the calling thread and uploads. The app decodes on a loader thread instead
    // (AudioCodec::decodeFile) and calls uploadDecoded() here.
    bool load(const std::filesystem::path& fileName, bool stream = false);
    bool uploadDecoded(DecodedAudio decoded);
    void unload();

    void play();
    void stop();
    void setPaused(bool paused);
    // Jump to `pct` (0..1) of the file without blocking the stream service. While playing the
    // request is handed to the service, which drops the queued audio and restarts from the new
    // spot within a few milliseconds. When stopped or paused, the queue is rebuilt at the new
    // spot so the next play starts there.
    void seekTo(float pct);
    void setPosition(float pct) { seekTo(pct); }
    void setPositionMS(int ms);

    // Once per UI tick, for every player.
    void update();
    static void updateAll();

    void setVolume(float vol);
    void setPan(float pan);
    void setSpeed(float spd);
    // Stereo image width: 1 = the normal ±30° spread, 0 = both channels in the pan position.
    void setStereoWidth(float width);
    float getStereoWidth() const { return stereoWidth; }
    // Mono moves its position; stereo turns both channels together.
    bool canPan() const { return channels == 1 || channels == 2; }

    // Loop region and loop switch. Safe to call at any time, including before loading and
    // while playing: the stream service picks the change up at its next chunk without the
    // caller waiting on it. When stopped, the queue is refilled at the new begin.
    void setLoopRegion(const LoopRegion& region);
    LoopRegion getLoopRegion() const { return renderer.region(); }
    void setLoop(bool loop);
    bool isLooping() const { return getLoopRegion().loop; }
    // Frame playback begins at: 0, or S when "play from start" is off.
    int64_t getBeginFrame() const { return renderer.beginFrame(); }

    float getPosition() const;
    int getPositionMS() const;
    // Frame (0..total) the listener is hearing right now, compensating for queued buffers and
    // device latency. Returns -1 when unknown. Never takes the voice's lock.
    double getAudibleFrame() const;
    // getAudibleFrame() as 0..1 of the file, or -1 when unknown.
    float getAudiblePosition() const;

    bool isPlaying() const;
    bool isPaused() const;
    bool isLoaded() const { return bLoadedOk; }
    // Reached its end (the service is done with it) but the source still plays the last of the queue.
    bool isPlayingOut() const;
    // True once this run has reached its end and said so. A seek or restart afterwards makes
    // it false again, withdrawing that notice.
    bool hasEnded() const;

    // Scheduled handover (see SoundPlayer): the device-clock time the last frame of this run
    // will be mixed, when it's heading for a known end.
    bool predictEndDeviceTime(int64_t& endNs) const;
    // Starts from the begin frame so its first frame is mixed at `startNs`.
    bool playAtDeviceTime(int64_t startNs);

    float getSpeed() const { return speed; }
    float getPan() const { return pan; }
    float getVolume() const { return volume; }
    float getDuration() const { return duration; }
    int getSampleRate() const { return samplerate; }
    int getNumChannels() const { return channels; }
    int64_t getTotalFrames() const { return totalFrames; }
    const std::filesystem::path& getFilePath() const { return fileName; }
    std::string getFormatString() const { return formatString; }
    std::string getSubFormatString() const { return subformatString; }

    // Effect sends, one per bus (see AudioDevice).
    float getSend(int bus) const { return bus >= 0 && bus < kSendCount ? sends[bus] : 0.0f; }
    void setSend(int bus, float send);
    float getReverbSend() const { return sends[0]; }
    void setReverbSend(float send) { setSend(0, send); }
    float getReverbSend2() const { return sends[kEaxReverbCount]; }
    void setReverbSend2(float send) { setSend(kEaxReverbCount, send); }

    // "Playback ended" notices, called on the stream service thread. A listener must only
    // queue the notice: it must not call back into a player or wait on anything.
    static void addPlaybackEndedListener(void* owner, PlaybackEndedCallback cb);
    static void removePlaybackEndedListener(void* owner);

    // Voice budget (see the .cpp): the first kEagerVoices samples get their source when they
    // load; past that, on first play, and voices idle for kIdleReleaseMs are given back.
    static constexpr int kEagerVoices = 512;
    static constexpr int64_t kIdleReleaseMs = 30000;
    static int voiceCount() { return g_voiceCount; }
    bool hasVoice() const { return source != 0; }

    // Buffers per voice: two are queued when the queue is (re)built, the rest are filled by
    // the stream service once playing. ~370 ms at 44.1 kHz.
    static constexpr int kQueueBuffers = 4;

private:
    // The decoded copy in memory, or the open file.
    class SampleSource final : public FrameSource {
    public:
        int64_t read(float* dst, int64_t frames) override;
        int64_t seek(int64_t frame) override;
        int64_t position() const override { return pos.load(); }
        void clear();

        bool memory = false;
        std::vector<short> memShort;
        std::vector<float> memFloat;
        int64_t memFrames = 0;
        AudioCodec::FileReader file;
        std::vector<short> scratch;
        int channels = 0;
        int64_t total = 0;
        std::atomic<int64_t> pos{0};
    };

    // --- Stream service ---
    static void serviceLoop();
    void serviceOnce();
    void startThread();
    void stopThread();
    void waitForThread();
    bool isThreadRunning() const { return threadRunning; }

    // --- Voice ---
    bool createVoice(const DecodedAudio* initial);
    bool ensureVoice();
    void destroyVoice();
    void destroyVoiceLocked(); // caller holds `mutex`, service stopped
    void releaseVoice();
    bool isIdleVoice() const;
    static void trimIdleVoices(int count);
    bool openStreamFile();
    static int64_t steadyMs();

    // --- Queue ---
    // Refills the queue from `startFrame` (-1: the begin frame); the service must not be running.
    bool primeStream(int64_t startFrame = -1);
    // Stops the source, drops its queued buffers and refills them. Caller holds `mutex`.
    // Leaves the source stopped.
    bool rebuildQueueLocked(int64_t startFrame, bool isSeek);
    bool queueInitialLocked(const DecodedAudio& initial);
    void haltPlayback();
    int chunkFrames() const;
    int renderChunk(RegionRenderer::ChunkRecord& rec);
    bool uploadRendered(ALuint buffer);
    void refillSparesLocked();
    void reportEndLocked(std::unique_lock<std::mutex>& lock);
    void applySends();
    void applyStereoImage();
    void applyStereoFallback(float* data, int frames);

    // --- Playhead history: guarded by historyMutex, held only around queue changes and reads ---
    static constexpr int kHistory = 12;
    void resetHistoryLocked();
    void pushHistoryLocked(const RegionRenderer::ChunkRecord& rec);
    double mapOutputFrameLocked(double outFrame) const;

    static void notifyPlaybackEnded(OpenALSoundPlayer* player);

    // File and format.
    std::filesystem::path fileName;
    std::string fileExtension;
    std::string formatString;
    std::string subformatString;
    FormatType sampleFormat = Int16;
    ALenum openALformat = AL_NONE;
    int channels = 0;
    int samplerate = 0;
    float duration = 0.0f;
    int64_t totalFrames = 0;
    int mp3BufferSize = 0;
    bool bLoadedOk = false;

    SampleSource src;
    RegionRenderer renderer;

    // Levels, kept while there's no voice and applied when it's made.
    float volume = 1.0f;
    float pan = 0.0f;
    float speed = 1.0f;
    float stereoWidth = 1.0f;
    // Pan and width for the renderer's own panner (no AL_EXT_STEREO_ANGLES).
    std::atomic<float> fallbackPan{0.0f};
    std::atomic<float> fallbackWidth{1.0f};
    float sends[kSendCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ALuint filters[kSendCount] = { 0, 0, 0, 0 };
    bool sendsDirty = true;

    // The voice.
    ALuint source = 0;
    std::vector<ALuint> buffers;
    std::vector<ALuint> spareBuffers; // not queued: filled by the service, or kept after the end
    std::vector<short> uploadShort;   // the rendered chunk, converted for 16-bit buffers
    int lastRendered = 0;             // frames in the renderer's last chunk
    int64_t lastActiveMs = 0;
    static int g_voiceCount;

    // Run state.
    std::atomic<bool> threadRunning{false};
    mutable std::mutex mutex;
    bool bPaused = false;
    bool streamPrimed = false;
    std::atomic<bool> streamEnd{false};
    std::atomic<bool> endNotified{false}; // "playback ended" already sent for this run
    std::atomic<int64_t> pendingStartNs{0};
    std::atomic<int64_t> pendingSeekFrame{-1};

    mutable std::mutex historyMutex;
    RegionRenderer::ChunkRecord history[kHistory];
    int historyCount = 0;    // records kept (queued + recently played)
    int historyNewest = -1;  // index of the newest record
    int queuedRecords = 0;   // how many of the newest records are still in the AL queue
    bool historyEnded = false; // the newest record is the last of the run
    int64_t lastFileFrame = 0; // file frame just after the last rendered frame
};
