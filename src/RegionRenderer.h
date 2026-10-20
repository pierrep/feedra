#pragma once

// The region renderer: turns a sample's frames into the audio a voice queues, applying the
// loop region on the way. It stops at E, wraps from E back to S with an equal-power crossfade,
// fades in or out where playback would otherwise start or stop mid-waveform, and applies the
// sample's own fade in and out.
//
// It knows nothing about OpenAL or threads: a voice feeds it a FrameSource and calls render()
// from wherever the queue is filled (holding its own lock). That's what makes it testable on
// its own (tests/test_region_renderer.cpp).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// Which part of a file plays, and how. Times are in seconds so a region survives
// reloads and does not depend on the file's sample rate.
struct LoopRegion {
    double start = 0.0;        // S: loop start
    double end = -1.0;         // E: loop end and where playback stops; <= 0 means the end of the file
    bool loop = false;         // loop S..E seamlessly
    bool playFromStart = true; // begin at 0:00 (anything before S is an intro) or at S
    bool playToEnd = true;     // when not looping, play on past E to the end of the file (an outro) or stop at E
    // Crossfade at the loop join, and fade-out at a moved end point. The renderer also keeps
    // it within half the loop.
    double crossfadeMs = 10.0;
    static constexpr double kMaxCrossfadeMs = 10000.0;
    // The sample's own fade in and fade out, in seconds (0 = none): the first and last stretch of
    // the file ramp up from and down to silence, wherever it plays from (equal power). They
    // belong to the audio, not to how it's started or stopped, and are drawn on the waveform.
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    static constexpr double kMaxFadeSec = 30.0;
    bool operator==(const LoopRegion& o) const
    {
        return start == o.start && end == o.end && loop == o.loop && playFromStart == o.playFromStart
            && playToEnd == o.playToEnd && crossfadeMs == o.crossfadeMs && fadeInSec == o.fadeInSec
            && fadeOutSec == o.fadeOutSec;
    }
    bool operator!=(const LoopRegion& o) const { return !(*this == o); }
};

// Where the renderer reads frames from: the decoded copy in memory, or the file.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    // Up to `frames` interleaved float frames into `dst`; returns how many were read.
    virtual int64_t read(float* dst, int64_t frames) = 0;
    // Moves to `frame`, clamped to the file; returns where it landed.
    virtual int64_t seek(int64_t frame) = 0;
    virtual int64_t position() const = 0;
};

class RegionRenderer {
public:
    // Which file frames one rendered chunk holds, for the playhead.
    struct Segment { int64_t out = 0; int64_t file = 0; int64_t frames = 0; };
    static constexpr int kMaxSegments = 8;
    struct ChunkRecord {
        int64_t outStart = 0;   // output frames rendered in this run before this chunk
        int64_t frames = 0;
        int segCount = 0;
        Segment seg[kMaxSegments];
        int64_t toEnd = -1;     // frames still to render until the end after this chunk; -1 = none/unknown
    };

    // S, E and the crossfade in frames for `r` in a file of `total` frames (0 = unknown).
    static void effectiveRegion(const LoopRegion& r, int rate, int64_t total,
                                int64_t& S, int64_t& E, int64_t& xf);

    // On load. The source must outlive any render call.
    void setFormat(int channels, int rate, int64_t totalFrames);
    void setTotalFrames(int64_t totalFrames) { m_total = totalFrames; }
    void setSource(FrameSource* source) { m_src = source; }

    // --- Any thread ---
    // Returns false when nothing changed. Applied at the next chunk (or the next run).
    bool setRegion(const LoopRegion& region);
    LoopRegion region() const;
    // A change is waiting to be applied (the end of this run isn't known yet).
    bool regionPending() const { return m_version.load() != m_appliedVersion.load(); }
    // Forget what was applied (a new file): the region is applied afresh next run.
    void resetApplied() { m_appliedVersion.store(0); m_headCacheValid = false; }
    // Frame playback begins at: 0, or S when "play from start" is off.
    int64_t beginFrame() const;

    // --- The thread filling the queue (caller holds the voice's lock) ---
    void applyPending();
    // A new run from `startFrame` (-1: the begin frame), as for play, stop or seek.
    // Returns the frame it starts at.
    int64_t startRun(int64_t startFrame, bool isSeek);
    // A new run whose first `framesAlready` frames from 0 were decoded elsewhere (the loader's
    // first chunks). Returns false when the region touches them, so they can't be used.
    bool canUseInitial(int64_t framesAlready) const;
    void startRunAfterInitial(int64_t framesAlready);
    // Carry on from the begin frame straight after an end (resuming a played-out pad).
    void restartAtBegin();
    // Renders up to `want` frames into data(). Returns how many (0 once the run has ended).
    int render(ChunkRecord& rec, int want);
    float* data() { return m_work.data(); }
    bool ended() const { return m_ended; }
    bool inCrossfade() const { return m_xfPos >= 0; }
    int64_t stopFrame() const;
    bool looping() const { return m_loop; }

private:
    void computeEffectiveRegion();
    void applyPendingRegion();
    void ensureHeadCache();
    static void addSegment(ChunkRecord& rec, int64_t out, int64_t file, int64_t frames);
    void applyFadeIn(float* data, int64_t frames);
    // The sample's own fades, by where `data` sits in the file: frames [pos, pos + frames).
    void applyEnvelope(float* data, int64_t pos, int64_t frames) const;

    FrameSource* m_src = nullptr;
    int m_channels = 0;
    int m_rate = 0;
    int64_t m_total = 0;

    // Region settings: written under m_regionMutex, applied by the renderer.
    mutable std::mutex m_regionMutex;
    LoopRegion m_requested;
    std::atomic<uint32_t> m_version{1};
    std::atomic<uint32_t> m_appliedVersion{0};
    LoopRegion m_active;
    int64_t m_S = 0, m_E = 0;     // effective region in frames
    int64_t m_xf = 0;             // effective loop crossfade length
    int64_t m_edgeFade = 0;       // short fade-in at a mid-file start, fade-out at a moved end
    int64_t m_envIn = 0;          // the sample's fade in, in frames, from the start of the file
    int64_t m_envOut = 0;         // the sample's fade out, in frames, to the end of the file
    bool m_loop = false;
    bool m_fromStart = true;
    bool m_toEnd = true;
    std::vector<float> m_headCache; // [S, S + xf), interleaved
    bool m_headCacheValid = false;

    // Run state.
    bool m_ended = false;
    int64_t m_xfPos = -1;           // position inside a loop crossfade, -1 when not in one
    int64_t m_fadeInLen = 0, m_fadeInDone = 0;
    int64_t m_outCounter = 0;
    std::vector<float> m_work;
};
