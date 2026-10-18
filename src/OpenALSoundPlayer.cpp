#include "OpenALSoundPlayer.h"

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <set>
#include <thread>

#include "AL/alext.h"
#include "AL/efx.h"

using AudioCodec::pathUtf8;

// Stereo panning by moving the two channels (OpenAL Soft's AL_EXT_STEREO_ANGLES).
#define FEEDRA_AL_STEREO_ANGLES 0x1030

namespace {

std::set<OpenALSoundPlayer*>& players()
{
    static std::set<OpenALSoundPlayer*>* all = new std::set<OpenALSoundPlayer*>;
    return *all;
}

std::mutex g_endedMutex;
std::map<void*, OpenALSoundPlayer::PlaybackEndedCallback> g_endedListeners;

} // namespace

//------------------------------------------------------------
// "Playback ended" listeners
//------------------------------------------------------------
void OpenALSoundPlayer::addPlaybackEndedListener(void* owner, PlaybackEndedCallback cb)
{
    std::lock_guard<std::mutex> lock(g_endedMutex);
    g_endedListeners[owner] = std::move(cb);
}

void OpenALSoundPlayer::removePlaybackEndedListener(void* owner)
{
    std::lock_guard<std::mutex> lock(g_endedMutex);
    g_endedListeners.erase(owner);
}

void OpenALSoundPlayer::notifyPlaybackEnded(OpenALSoundPlayer* player)
{
    // The callbacks run under the lock, so once removePlaybackEndedListener() has returned
    // no stream thread can still be calling into that listener (a pad deleted on the UI
    // thread while another pad's sample ends). Listeners must only queue the notice: they
    // must not take this lock or wait on a stream thread.
    std::lock_guard<std::mutex> lock(g_endedMutex);
    for (auto& kv : g_endedListeners) {
        kv.second(player);
    }
}

//------------------------------------------------------------
// Stream service: one thread keeps every playing sample's queue topped up.
//
// An active player is in g_active, and a single thread services each one in turn every
// couple of milliseconds. With kQueueBuffers buffers per voice there is ~370 ms of audio
// queued, so a slow moment (an mp3 seek, a crossfade's head read) on one sample can't
// starve another.
//
// Locks: g_listMutex guards the set and the start/stop of membership (short). g_serviceMutex
// is held while one player is being serviced, so waitForThread() can wait for the call in
// progress to finish. Order: g_serviceMutex, then g_listMutex, then the player's own mutex.
//------------------------------------------------------------
namespace {

std::mutex g_listMutex;
std::mutex g_serviceMutex;
std::condition_variable g_wake;
std::set<OpenALSoundPlayer*> g_active;

struct StreamService {
    std::thread thread;
    std::atomic<bool> quit{false};

    void ensureStarted(void (*body)())
    {
        // Under g_listMutex (see startThread).
        if (!thread.joinable()) {
            thread = std::thread(body);
        }
    }

    ~StreamService()
    {
        {
            std::lock_guard<std::mutex> lock(g_listMutex);
            quit.store(true);
        }
        g_wake.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }
};

StreamService& streamService()
{
    static StreamService service;
    return service;
}

} // namespace

void OpenALSoundPlayer::serviceLoop()
{
    StreamService& service = streamService();
    std::vector<OpenALSoundPlayer*> snapshot;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(g_listMutex);
            g_wake.wait(lock, [&]() { return service.quit.load() || !g_active.empty(); });
            if (service.quit.load()) {
                return;
            }
            snapshot.assign(g_active.begin(), g_active.end());
        }
        for (OpenALSoundPlayer* player : snapshot) {
            std::lock_guard<std::mutex> busy(g_serviceMutex);
            {
                std::lock_guard<std::mutex> lock(g_listMutex);
                // Removed by waitForThread() (or deleted) since the snapshot: never touch it.
                if (g_active.find(player) == g_active.end()) {
                    continue;
                }
                if (!player->threadRunning.load()) {
                    g_active.erase(player);
                    continue;
                }
            }
            player->serviceOnce();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void OpenALSoundPlayer::startThread()
{
    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        if (threadRunning && g_active.count(this)) {
            return;
        }
        threadRunning = true;
        g_active.insert(this);
        streamService().ensureStarted(&OpenALSoundPlayer::serviceLoop);
    }
    g_wake.notify_one();
}

void OpenALSoundPlayer::stopThread()
{
    // The service drops it on its next pass. Use waitForThread() to be sure it's done.
    threadRunning = false;
}

void OpenALSoundPlayer::waitForThread()
{
    threadRunning = false;
    {
        std::lock_guard<std::mutex> lock(g_listMutex);
        g_active.erase(this);
    }
    // If the service is inside serviceOnce() for this player right now, wait for it to
    // finish. Afterwards it can't start again: it checks the set first. (Never called from
    // the service thread: playback-ended listeners only queue the notice.)
    std::lock_guard<std::mutex> barrier(g_serviceMutex);
}

void OpenALSoundPlayer::reportEndLocked(std::unique_lock<std::mutex>& lock)
{
    // The last chunk is queued: report the end now and let it play out. Resuming a paused
    // tail must not report it a second time.
    stopThread();
    const bool notify = !endNotified;
    endNotified = true;
    lock.unlock();
    if (notify) {
        notifyPlaybackEnded(this);
    }
}

void OpenALSoundPlayer::serviceOnce()
{
    // One pass for this player, on the stream service thread (see serviceLoop).
    // Returning without stopThread() means "service again next pass".
    std::unique_lock<std::mutex> lock(mutex);
    // Pause and stop clear the running flag under this lock, so nothing below restarts them.
    if (!isThreadRunning() || !hasVoice()) {
        stopThread();
        return;
    }

    // A seek from the UI is applied here, by the thread that owns the decoder, so the UI never
    // waits on this lock. The queued audio is dropped and refilled from the new spot, then
    // restarted straight away.
    const int64_t seek = pendingSeekFrame.exchange(-1);
    if (seek >= 0) {
        const bool ok = rebuildQueueLocked(seek, true);
        if (ok && !bPaused) {
            alSourcePlay(source);
        }
        if (renderer.ended()) {
            reportEndLocked(lock);
        }
        return;
    }

    ALint state = AL_STOPPED;
    ALint processed = 0;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
    while (processed > 0 && !renderer.ended()) {
        RegionRenderer::ChunkRecord rec;
        if (renderChunk(rec) <= 0) {
            break;
        }
        std::lock_guard<std::mutex> hist(historyMutex);
        ALuint buffer = 0;
        alSourceUnqueueBuffers(source, 1, &buffer);
        uploadRendered(buffer);
        alSourceQueueBuffers(source, 1, &buffer);
        queuedRecords = std::max(0, queuedRecords - 1);
        pushHistoryLocked(rec);
        --processed;
    }

    if (renderer.ended() && processed > 0) {
        // Drop finished buffers so an underrun restart below can't replay them. They are kept
        // as spares in case playback carries on (see setPaused).
        std::lock_guard<std::mutex> hist(historyMutex);
        for (ALint k = 0; k < processed; ++k) {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(source, 1, &buffer);
            spareBuffers.push_back(buffer);
            queuedRecords = std::max(0, queuedRecords - 1);
        }
    }

    // Buffers not in the queue yet (a fresh start queues two and leaves the rest as spares),
    // filled here rather than on the UI thread.
    if (!renderer.ended() && !spareBuffers.empty()) {
        refillSparesLocked();
    }

    // Underrun: the queue ran dry before it was refilled. Play what is queued now.
    if (state == AL_STOPPED) {
        ALint queuedNow = 0;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queuedNow);
        if (queuedNow > 0) {
            alSourcePlay(source);
        }
    }

    if (renderer.ended()) {
        reportEndLocked(lock);
    }
}

//------------------------------------------------------------
// Sample source: memory or file
//------------------------------------------------------------
int64_t OpenALSoundPlayer::SampleSource::read(float* dst, int64_t frames)
{
    if (frames <= 0 || channels <= 0) {
        return 0;
    }
    int64_t got = 0;
    if (memory) {
        const int64_t at = pos.load();
        got = std::clamp<int64_t>(memFrames - at, 0, frames);
        const size_t first = static_cast<size_t>(at) * static_cast<size_t>(channels);
        const size_t count = static_cast<size_t>(got) * static_cast<size_t>(channels);
        if (!memShort.empty()) {
            for (size_t i = 0; i < count; ++i) {
                dst[i] = float(memShort[first + i]) * AudioCodec::kShortToFloat;
            }
        } else if (count > 0) {
            std::memcpy(dst, memFloat.data() + first, count * sizeof(float));
        }
    } else if (file.isOpen()) {
        got = file.readFloat(dst, frames, scratch);
    }
    got = std::max<int64_t>(0, got);
    pos.fetch_add(got);
    return got;
}

int64_t OpenALSoundPlayer::SampleSource::seek(int64_t frame)
{
    frame = std::max<int64_t>(0, frame);
    if (total > 0) {
        frame = std::min(frame, total);
    }
    int64_t at = frame;
    if (memory) {
        at = std::min(frame, memFrames);
    } else if (file.isOpen()) {
        at = file.seek(frame);
    }
    pos.store(at);
    return at;
}

void OpenALSoundPlayer::SampleSource::clear()
{
    file.close();
    memory = false;
    std::vector<short>().swap(memShort);
    std::vector<float>().swap(memFloat);
    memFrames = 0;
    total = 0;
    pos.store(0);
}

//------------------------------------------------------------
// Construction and loading
//------------------------------------------------------------
OpenALSoundPlayer::OpenALSoundPlayer()
{
    renderer.setSource(&src);
    players().insert(this);
}

OpenALSoundPlayer::~OpenALSoundPlayer()
{
    unload();
    players().erase(this);
    // The device is not closed here when the last player goes. It is opened once at startup
    // and closed once at shutdown (main.cpp).
    waitForThread();
}

void OpenALSoundPlayer::updateAll()
{
    AudioDevice::process();
    auto copy = players();
    for (auto* p : copy) {
        p->update();
    }
}

bool OpenALSoundPlayer::load(const std::filesystem::path& path, bool stream)
{
    AudioDevice::initialize();
    DecodedAudio decoded = AudioCodec::decodeFile(path, stream, AudioDevice::floatOutput());
    if (!decoded.ok) {
        return false;
    }
    return uploadDecoded(std::move(decoded));
}

bool OpenALSoundPlayer::uploadDecoded(DecodedAudio decoded)
{
    if (!decoded.ok) {
        return false;
    }
    AudioDevice::initialize();
    if (bLoadedOk || hasVoice() || src.memory || src.file.isOpen()) {
        unload();
    }
    bLoadedOk = false;

    fileName = decoded.path;
    fileExtension = decoded.fileExtension;
    formatString = decoded.formatString;
    subformatString = decoded.subformatString;
    sampleFormat = decoded.sampleFormat;
    channels = decoded.channels;
    samplerate = decoded.sampleRate;
    duration = decoded.duration;
    mp3BufferSize = decoded.mp3BufferSize;
    streamEnd = false;
    totalFrames = 0;

    if (channels <= 0 || samplerate <= 0) {
        qCritical() << "Sound file load failed - wrong file type or empty file";
        return false;
    }
    if (channels == 1) {
        openALformat = sampleFormat == Float ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_MONO16;
    } else if (channels == 2) {
        openALformat = sampleFormat == Float ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_STEREO16;
    } else {
        qCritical() << "OpenALSoundPlayer: unsupported channel layout for" << pathUtf8(fileName).c_str();
        return false;
    }

    // Every sample plays through the stream queue. Samples that aren't streamed from disk are
    // fed from the decoded copy in memory, so loops and fades work the same way.
    src.channels = channels;
    if (decoded.streaming) {
        src.memory = false;
        // Reopened without measuring: the loader already knows the length.
        if (!src.file.open(fileName, sampleFormat == Float, false)) {
            qCritical() << "OpenALSoundPlayer: couldn't open" << pathUtf8(fileName).c_str();
            return false;
        }
        totalFrames = decoded.totalFrames > 0
            ? decoded.totalFrames
            : static_cast<int64_t>(std::llround(double(decoded.duration) * decoded.sampleRate));
        src.total = totalFrames;
        src.pos.store(src.file.seek(decoded.resumeFrames));
    } else {
        src.memory = true;
        if (sampleFormat == Int16) {
            src.memShort = std::move(decoded.pcmShort);
            src.memFrames = static_cast<int64_t>(src.memShort.size()) / channels;
        } else {
            src.memFloat = std::move(decoded.pcmFloat);
            src.memFrames = static_cast<int64_t>(src.memFloat.size()) / channels;
        }
        if (src.memFrames <= 0) {
            qCritical() << "Sound file load failed - wrong file type or empty file";
            src.clear();
            return false;
        }
        totalFrames = src.memFrames;
        src.total = totalFrames;
        src.pos.store(0);
        duration = float(double(totalFrames) / double(samplerate));
    }
    renderer.setFormat(channels, samplerate, totalFrames);

    // Send levels belong to the pad and are set again right after loading.
    for (float& send : sends) {
        send = 0.0f;
    }
    sendsDirty = true;

    bLoadedOk = true;
    if (g_voiceCount < kEagerVoices) {
        // The usual case: the voice (source, buffers, first audio queued) is made now, using the
        // chunks the loader already decoded, so the first play starts straight away.
        if (createVoice(&decoded)) {
            return true;
        }
    }
    // A big project, or no sources left: the sample is loaded but gets its voice the first
    // time it plays (see ensureVoice). Its file is closed until then.
    if (!src.memory) {
        src.file.close();
    }
    streamPrimed = false;
    return true;
}

void OpenALSoundPlayer::unload()
{
    haltPlayback();
    waitForThread();
    {
        std::unique_lock<std::mutex> lock(mutex);
        destroyVoiceLocked();
    }
    streamPrimed = false;
    src.clear();
    fileExtension.clear();
    totalFrames = 0;
    renderer.setTotalFrames(0);
    // The requested loop region stays: it belongs to the sample, not to this load.
    renderer.resetApplied();
    bLoadedOk = false;
}

//------------------------------------------------------------
// Voices
//
// A voice is what a sample needs to make sound: its OpenAL source and queue buffers, and for
// streamed samples the open file. The first kEagerVoices samples get one when they load. Past
// that (a big campaign file), a sample gets its voice the first time it plays, and voices left
// unused for kIdleReleaseMs are given back while over the budget. If OpenAL runs out of
// sources anyway, the longest-idle voices are freed and it tries again. All of this runs on
// the UI thread.
//------------------------------------------------------------
int OpenALSoundPlayer::g_voiceCount = 0;

int64_t OpenALSoundPlayer::steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool OpenALSoundPlayer::createVoice(const DecodedAudio* initial)
{
    if (hasVoice()) {
        return true;
    }
    // Generated into a local: `source` stays 0 (no voice) until it exists, so trimIdleVoices()
    // below can't mistake this player for an idle one.
    ALuint made = 0;
    alGetError();
    alGenSources(1, &made);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        // Out of sources: free the longest-idle voices and try once more.
        trimIdleVoices(16);
        alGetError();
        alGenSources(1, &made);
        err = alGetError();
    }
    if (err != AL_NO_ERROR || made == 0) {
        qWarning() << "OpenALSoundPlayer: couldn't get a source for" << pathUtf8(fileName).c_str() << ":"
                   << AudioDevice::alErrorString(err).c_str() << "(" << g_voiceCount << "voices in use)";
        return false;
    }
    source = made;
    ++g_voiceCount;
    lastActiveMs = steadyMs();
    buffers.assign(static_cast<size_t>(kQueueBuffers), 0);
    alGenBuffers(static_cast<ALsizei>(buffers.size()), buffers.data());

    // The levels set while it had no voice (or the defaults for a new sample).
    alSourcef(source, AL_PITCH, speed);
    alSourcef(source, AL_MAX_GAIN, 6.0f);
    alSourcef(source, AL_GAIN, volume);
    alSourcef(source, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        qCritical() << "OpenALSoundPlayer: couldn't set up the source for" << pathUtf8(fileName).c_str()
                    << AudioDevice::alErrorString(err).c_str();
        destroyVoice();
        return false;
    }

    // One send per effect bus.
    for (int bus = 0; bus < kSendCount; ++bus) {
        if (AudioDevice::sendAvailable(bus)) {
            filters[bus] = AudioDevice::newSendFilter();
        }
    }
    applySends();
    alGetError();

    setPan(pan);

    bool queued = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        endNotified = false;
        if (initial) {
            queued = queueInitialLocked(*initial);
        }
        if (!queued) {
            queued = rebuildQueueLocked(-1, false);
        }
    }
    if (!queued) {
        qCritical() << "OpenALSoundPlayer: nothing to play in" << pathUtf8(fileName).c_str();
        destroyVoice();
        return false;
    }
    streamPrimed = true;
    return true;
}

bool OpenALSoundPlayer::ensureVoice()
{
    if (hasVoice()) {
        return true;
    }
    if (!bLoadedOk) {
        return false;
    }
    if (!src.memory && !src.file.isOpen() && !openStreamFile()) {
        return false;
    }
    if (!createVoice(nullptr)) {
        if (!src.memory) {
            src.file.close();
        }
        return false;
    }
    return true;
}

bool OpenALSoundPlayer::openStreamFile()
{
    if (!src.file.open(fileName, sampleFormat == Float, false)) {
        qCritical() << "OpenALSoundPlayer: couldn't reopen" << pathUtf8(fileName).c_str();
        return false;
    }
    if (src.file.channels() != channels) {
        qCritical() << "OpenALSoundPlayer:" << pathUtf8(fileName).c_str() << "changed on disk since it was loaded";
        src.file.close();
        return false;
    }
    src.pos.store(src.file.seek(0));
    streamEnd = false;
    renderer.resetApplied(); // the head cache is read from the file again
    return true;
}

void OpenALSoundPlayer::destroyVoiceLocked()
{
    if (source != 0) {
        // Sources before buffers.
        alDeleteSources(1, &source);
        source = 0;
        --g_voiceCount;
    }
    if (!buffers.empty()) {
        alDeleteBuffers(static_cast<ALsizei>(buffers.size()), buffers.data());
    }
    buffers.clear();
    spareBuffers.clear(); // they were among the buffers just deleted
    for (ALuint& filter : filters) {
        AudioDevice::deleteFilter(filter);
    }
    sendsDirty = true;
    std::lock_guard<std::mutex> hist(historyMutex);
    resetHistoryLocked();
}

void OpenALSoundPlayer::destroyVoice()
{
    waitForThread();
    std::unique_lock<std::mutex> lock(mutex);
    destroyVoiceLocked();
    streamPrimed = false;
}

void OpenALSoundPlayer::releaseVoice()
{
    if (!hasVoice()) {
        return;
    }
    destroyVoice();
    pendingSeekFrame.store(-1);
    pendingStartNs.store(0);
    if (!src.memory) {
        src.file.close();
    }
}

bool OpenALSoundPlayer::isIdleVoice() const
{
    if (!hasVoice() || isThreadRunning() || pendingStartNs.load() != 0) {
        return false;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state != AL_PLAYING && state != AL_PAUSED;
}

void OpenALSoundPlayer::trimIdleVoices(int count)
{
    std::vector<OpenALSoundPlayer*> idle;
    for (OpenALSoundPlayer* p : players()) {
        if (p->isIdleVoice()) {
            idle.push_back(p);
        }
    }
    std::sort(idle.begin(), idle.end(), [](const OpenALSoundPlayer* a, const OpenALSoundPlayer* b) {
        return a->lastActiveMs < b->lastActiveMs;
    });
    const int freed = std::min<int>(count, static_cast<int>(idle.size()));
    for (int i = 0; i < freed; ++i) {
        idle[static_cast<size_t>(i)]->releaseVoice();
    }
    if (freed > 0) {
        qInfo() << "OpenALSoundPlayer: freed" << freed << "idle voices";
    }
}

//------------------------------------------------------------
// Queue
//------------------------------------------------------------
int OpenALSoundPlayer::chunkFrames() const
{
    int frames = 4096;
    if (src.file.isOpen() && src.file.isMp3() && mp3BufferSize > 0 && channels > 0) {
        frames = std::max(256, mp3BufferSize / channels);
    }
    if (speed > 1.0f) {
        frames *= static_cast<int>(std::lround(speed));
    }
    return frames;
}

int OpenALSoundPlayer::renderChunk(RegionRenderer::ChunkRecord& rec)
{
    const int out = renderer.render(rec, chunkFrames());
    lastRendered = out;
    streamEnd = renderer.ended();
    applyStereoFallback(renderer.data(), out);
    return out;
}

bool OpenALSoundPlayer::uploadRendered(ALuint buffer)
{
    // The renderer's last chunk, as rendered (float) or converted to 16-bit.
    const size_t count = static_cast<size_t>(std::max(0, lastRendered)) * static_cast<size_t>(std::max(1, channels));
    if (count == 0) {
        return false;
    }
    const float* data = renderer.data();
    alGetError();
    if (openALformat == AL_FORMAT_MONO16 || openALformat == AL_FORMAT_STEREO16) {
        uploadShort.resize(count);
        for (size_t i = 0; i < count; ++i) {
            uploadShort[i] = AudioCodec::floatToShort(data[i]);
        }
        alBufferData(buffer, openALformat, uploadShort.data(), static_cast<ALsizei>(count * sizeof(short)), samplerate);
    } else {
        alBufferData(buffer, openALformat, data, static_cast<ALsizei>(count * sizeof(float)), samplerate);
    }
    return alGetError() == AL_NO_ERROR;
}

void OpenALSoundPlayer::refillSparesLocked()
{
    // Buffers kept out of the queue (spares) go back in, so the stream has its full queue again.
    while (!spareBuffers.empty() && !renderer.ended()) {
        RegionRenderer::ChunkRecord rec;
        if (renderChunk(rec) <= 0) {
            break;
        }
        std::lock_guard<std::mutex> hist(historyMutex);
        ALuint buffer = spareBuffers.front();
        uploadRendered(buffer);
        alSourceQueueBuffers(source, 1, &buffer);
        spareBuffers.erase(spareBuffers.begin());
        pushHistoryLocked(rec);
    }
}

bool OpenALSoundPlayer::primeStream(int64_t startFrame)
{
    if (!hasVoice() || buffers.size() < 2) {
        return false;
    }
    std::unique_lock<std::mutex> lock(mutex);
    pendingSeekFrame.store(-1);
    if (!rebuildQueueLocked(startFrame, startFrame >= 0)) {
        return false;
    }
    streamPrimed = true;
    return true;
}

bool OpenALSoundPlayer::rebuildQueueLocked(int64_t startFrame, bool isSeek)
{
    spareBuffers.clear(); // every buffer is queued (or made a spare) again below
    {
        std::lock_guard<std::mutex> hist(historyMutex);
        alSourceStop(source);
        alSourceRewind(source);
        alSourcei(source, AL_BUFFER, 0);
        resetHistoryLocked();
    }
    pendingStartNs.store(0);
    endNotified = false;
    renderer.startRun(startFrame, isSeek);

    int queued = 0;
    for (int s = 0; s < 2 && !renderer.ended(); ++s) {
        RegionRenderer::ChunkRecord rec;
        if (renderChunk(rec) <= 0) {
            break;
        }
        std::lock_guard<std::mutex> hist(historyMutex);
        ALuint buffer = buffers[static_cast<size_t>(s)];
        if (uploadRendered(buffer)) {
            alSourceQueueBuffers(source, 1, &buffer);
        }
        pushHistoryLocked(rec);
        ++queued;
    }
    streamEnd = renderer.ended();
    // The rest of the buffers are filled by the stream service once playing.
    for (size_t b = static_cast<size_t>(queued); b < buffers.size(); ++b) {
        spareBuffers.push_back(buffers[b]);
    }
    return queued > 0;
}

bool OpenALSoundPlayer::queueInitialLocked(const DecodedAudio& initial)
{
    // The two chunks the loader already decoded from the start of the file are used as they
    // are when the region can't touch them (the usual case), which keeps decoding off the UI
    // thread. Anything else is rendered by rebuildQueueLocked().
    if (src.memory || initial.streamEnded || initial.initialChunks.size() < 2 || channels <= 0) {
        return false;
    }
    int64_t initialFrames = 0;
    for (size_t c = 0; c < 2; ++c) {
        initialFrames += static_cast<int64_t>(initial.initialChunks[c].pcmShort.size()) / channels;
    }
    renderer.applyPending();
    if (src.position() != initialFrames || !renderer.canUseInitial(initialFrames)) {
        return false;
    }
    std::lock_guard<std::mutex> hist(historyMutex);
    resetHistoryLocked();
    int64_t start = 0;
    const int64_t stop = renderer.stopFrame();
    for (size_t c = 0; c < 2; ++c) {
        const DecodedChunk& chunk = initial.initialChunks[c];
        const int64_t frames = static_cast<int64_t>(chunk.pcmShort.size()) / channels;
        ALuint buffer = buffers[c];
        alGetError();
        if (openALformat == AL_FORMAT_MONO16 || openALformat == AL_FORMAT_STEREO16) {
            alBufferData(buffer, openALformat, chunk.pcmShort.data(),
                         static_cast<ALsizei>(chunk.pcmShort.size() * sizeof(short)), samplerate);
        } else {
            alBufferData(buffer, openALformat, chunk.pcmFloat.data(),
                         static_cast<ALsizei>(chunk.pcmFloat.size() * sizeof(float)), samplerate);
        }
        if (alGetError() == AL_NO_ERROR) {
            alSourceQueueBuffers(source, 1, &buffer);
        }
        RegionRenderer::ChunkRecord rec;
        rec.outStart = start;
        rec.frames = frames;
        rec.toEnd = renderer.looping() || stop >= INT64_MAX / 8 ? -1 : std::max<int64_t>(0, stop - (start + frames));
        rec.segCount = 1;
        rec.seg[0] = RegionRenderer::Segment{ start, start, frames };
        pushHistoryLocked(rec);
        start += frames;
    }
    renderer.startRunAfterInitial(start);
    pendingStartNs.store(0);
    spareBuffers.clear();
    for (size_t b = 2; b < buffers.size(); ++b) {
        spareBuffers.push_back(buffers[b]); // filled by the stream service once playing
    }
    return true;
}

void OpenALSoundPlayer::haltPlayback()
{
    if (!hasVoice() || !bLoadedOk) {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        // Cleared under the lock so the stream service can't restart the source after this.
        threadRunning = false;
        alSourceStop(source);
    }
    pendingSeekFrame.store(-1);
    pendingStartNs.store(0);
    streamEnd = true;
}

//------------------------------------------------------------
// Playhead history
//------------------------------------------------------------
void OpenALSoundPlayer::resetHistoryLocked()
{
    historyCount = 0;
    historyNewest = -1;
    queuedRecords = 0;
    historyEnded = false;
}

void OpenALSoundPlayer::pushHistoryLocked(const RegionRenderer::ChunkRecord& rec)
{
    historyNewest = (historyNewest + 1) % kHistory;
    history[historyNewest] = rec;
    historyCount = std::min(historyCount + 1, kHistory);
    queuedRecords = std::min(queuedRecords + 1, historyCount);
    historyEnded = rec.toEnd == 0;
    if (rec.segCount > 0) {
        const RegionRenderer::Segment& last = rec.seg[rec.segCount - 1];
        lastFileFrame = last.file + last.frames;
    }
}

double OpenALSoundPlayer::mapOutputFrameLocked(double outFrame) const
{
    if (historyCount <= 0) {
        return -1.0;
    }
    const RegionRenderer::ChunkRecord& newest = history[historyNewest];
    if (outFrame >= double(newest.outStart + newest.frames)) {
        return double(lastFileFrame);
    }
    for (int k = 0; k < historyCount; ++k) {
        const RegionRenderer::ChunkRecord& r = history[(historyNewest - k + kHistory) % kHistory];
        if (outFrame < double(r.outStart) && k + 1 < historyCount) {
            continue;
        }
        if (r.segCount == 0) {
            return -1.0;
        }
        const double off = std::max(0.0, outFrame - double(r.outStart));
        for (int s = 0; s < r.segCount; ++s) {
            const RegionRenderer::Segment& seg = r.seg[s];
            if (off < double(seg.out - r.outStart + seg.frames) || s + 1 == r.segCount) {
                const double into = std::clamp(off - double(seg.out - r.outStart), 0.0, double(seg.frames));
                return double(seg.file) + into;
            }
        }
    }
    return -1.0;
}

double OpenALSoundPlayer::getAudibleFrame() const
{
    if (!hasVoice() || !bLoadedOk || samplerate <= 0) {
        return -1.0;
    }
    std::lock_guard<std::mutex> lock(historyMutex);
    if (historyCount == 0) {
        return -1.0;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    double frame = -1.0;
    if (queuedRecords == 0 || (state == AL_STOPPED && historyEnded)) {
        // Played out to the end.
        frame = double(lastFileFrame);
    } else {
        double offset = 0.0;
        double latencyFrames = 0.0;
        double offsetSec = 0.0;
        double latencySec = 0.0;
        if (AudioDevice::offsetLatency(source, offsetSec, latencySec)) {
            offset = offsetSec * samplerate;
            latencyFrames = latencySec * samplerate * std::max(0.0f, speed);
        } else {
            ALint sampleOffset = 0;
            alGetSourcei(source, AL_SAMPLE_OFFSET, &sampleOffset);
            offset = sampleOffset;
        }
        const RegionRenderer::ChunkRecord& head = history[(historyNewest - (queuedRecords - 1) + kHistory) % kHistory];
        double out = double(head.outStart) + offset;
        if (state == AL_PLAYING) {
            // Until the output delay has passed, nothing from this run is audible: hold at its
            // start rather than stepping back past it.
            out -= std::min(latencyFrames, out);
        }
        frame = mapOutputFrameLocked(out);
    }
    if (frame < 0.0) {
        return -1.0;
    }
    if (totalFrames > 0) {
        frame = std::min(frame, double(totalFrames));
    }
    return frame;
}

float OpenALSoundPlayer::getAudiblePosition() const
{
    const double frame = getAudibleFrame();
    if (frame < 0.0 || totalFrames <= 0) {
        return -1.0f;
    }
    return static_cast<float>(std::min(1.0, frame / double(totalFrames)));
}

float OpenALSoundPlayer::getPosition() const
{
    if (totalFrames <= 0) {
        return 0;
    }
    // No voice: stopped, so it sits at its begin frame.
    if (!hasVoice()) {
        return static_cast<float>(double(getBeginFrame()) / double(totalFrames));
    }
    double frame = getAudibleFrame();
    if (frame < 0.0) {
        frame = double(src.position());
    }
    return static_cast<float>(std::clamp(frame / double(totalFrames), 0.0, 1.0));
}

int OpenALSoundPlayer::getPositionMS() const
{
    if (samplerate <= 0) {
        return 0;
    }
    if (!hasVoice()) {
        return static_cast<int>(double(getBeginFrame()) * 1000.0 / samplerate);
    }
    double frame = getAudibleFrame();
    if (frame < 0.0) {
        frame = double(src.position());
    }
    return static_cast<int>(frame * 1000.0 / samplerate);
}

//------------------------------------------------------------
// Scheduled starts
//------------------------------------------------------------
bool OpenALSoundPlayer::predictEndDeviceTime(int64_t& endNs) const
{
    if (!AudioDevice::scheduledStartAvailable() || !hasVoice() || !bLoadedOk || samplerate <= 0) {
        return false;
    }
    if (renderer.regionPending()) {
        return false; // a settings change is on its way; the end isn't known yet
    }
    std::lock_guard<std::mutex> lock(historyMutex);
    if (historyCount == 0 || queuedRecords == 0) {
        return false;
    }
    const RegionRenderer::ChunkRecord& newest = history[historyNewest];
    if (newest.toEnd < 0) {
        return false;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        return false;
    }
    int64_t offsetFixed = 0;
    int64_t clock = 0;
    if (!AudioDevice::sampleOffsetClock(source, offsetFixed, clock)) {
        return false;
    }
    const double offset = double(offsetFixed) / 4294967296.0;
    int64_t queuedFrames = 0;
    for (int k = 0; k < queuedRecords; ++k) {
        queuedFrames += history[(historyNewest - k + kHistory) % kHistory].frames;
    }
    const double remaining = std::max(0.0, double(queuedFrames) - offset) + double(newest.toEnd);
    const double rate = double(samplerate) * std::max(0.01, double(speed));
    int64_t from = clock;
    const int64_t pending = pendingStartNs.load();
    if (pending > from) {
        from = pending; // scheduled but not started yet
    }
    endNs = from + static_cast<int64_t>(std::llround(remaining / rate * 1e9));
    return true;
}

bool OpenALSoundPlayer::playAtDeviceTime(int64_t startNs)
{
    if (!bLoadedOk || !AudioDevice::scheduledStartAvailable() || !ensureVoice()) {
        return false;
    }
    if (!streamPrimed) {
        waitForThread();
        primeStream();
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = false;
        if (!AudioDevice::playAtTime(1, &source, startNs)) {
            alSourcePlay(source);
        }
        pendingStartNs.store(startNs);
    }
    streamPrimed = false;
    startThread();
    return true;
}

//------------------------------------------------------------
// Transport
//------------------------------------------------------------
bool OpenALSoundPlayer::isPlayingOut() const
{
    if (!hasVoice() || !bLoadedOk || isThreadRunning()) {
        return false;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool OpenALSoundPlayer::hasEnded() const
{
    return streamEnd.load() && endNotified.load();
}

bool OpenALSoundPlayer::isPlaying() const
{
    // Also while the last queued audio is still sounding after the stream reached its end
    // (up to the queue's length), so pads don't show "stopped" before the sound stops.
    return hasVoice() && (isThreadRunning() || isPlayingOut());
}

bool OpenALSoundPlayer::isPaused() const
{
    if (!hasVoice()) {
        return false;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

void OpenALSoundPlayer::play()
{
    if (!bLoadedOk || !ensureVoice()) {
        return;
    }
    if (!streamPrimed) {
        waitForThread();
        primeStream();
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = false;
        pendingStartNs.store(0);
        alSourcePlay(source);
    }
    streamPrimed = false;
    startThread();
}

void OpenALSoundPlayer::stop()
{
    haltPlayback();
    if (bLoadedOk) {
        waitForThread();
        primeStream();
    }
}

void OpenALSoundPlayer::setPaused(bool paused)
{
    if (!bLoadedOk) {
        return;
    }
    if (!paused && !ensureVoice()) {
        return;
    }
    if (!hasVoice()) {
        return;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    if (!paused) {
        if (state == AL_PLAYING && isThreadRunning()) {
            return; // already playing; playing it again would restart the queue
        }
        if (state == AL_PLAYING && !isThreadRunning()) {
            // Reached its end and is still playing the last of the queue: carry on from the
            // begin frame straight after it.
            waitForThread();
            {
                std::unique_lock<std::mutex> lock(mutex);
                streamEnd = false;
                endNotified = false;
                renderer.restartAtBegin();
                bPaused = false;
                refillSparesLocked();
            }
            streamPrimed = false;
            startThread();
            return;
        }
        if (!streamPrimed && !isThreadRunning() && state != AL_PAUSED) {
            waitForThread();
            primeStream();
        }
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = paused;
        if (bPaused) {
            threadRunning = false;
            alSourcePause(source);
        } else {
            pendingStartNs.store(0);
            alSourcePlay(source);
        }
    }
    if (bPaused) {
        waitForThread();
    } else {
        streamPrimed = false;
        startThread();
    }
}

void OpenALSoundPlayer::seekTo(float pct)
{
    // A seek on a stopped sample sets where its next play starts, so it needs its queue.
    if (!bLoadedOk || totalFrames <= 0 || !ensureVoice()) {
        return;
    }
    pct = std::clamp(pct, 0.0f, 1.0f);
    int64_t frame = std::min<int64_t>(totalFrames - 1, static_cast<int64_t>(double(pct) * double(totalFrames)));

    // Keep inside the part of the file that plays: before E, and not in the intro when
    // "play from start" is off.
    int64_t S = 0, E = 0, xf = 0;
    const LoopRegion r = getLoopRegion();
    RegionRenderer::effectiveRegion(r, samplerate, totalFrames, S, E, xf);
    // Past E only into an outro: when not looping and playing to the end.
    if (r.loop || !r.playToEnd) {
        frame = std::min(frame, std::max<int64_t>(0, E - 1));
    }
    if (!r.playFromStart) {
        frame = std::max(frame, S);
    }

    if (isThreadRunning()) {
        // Latest request wins, so dragging just overwrites it.
        pendingSeekFrame.store(frame);
        return;
    }
    waitForThread();
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING) {
        // Still playing the last of the queue after its end was reached: carry on playing from
        // the new spot. Its "ended" notice is withdrawn (hasEnded() turns false).
        {
            std::unique_lock<std::mutex> lock(mutex);
            rebuildQueueLocked(frame, true);
            bPaused = false;
            pendingStartNs.store(0);
            alSourcePlay(source);
        }
        streamPrimed = false;
        // Also when the new spot is within the last chunks: the service then reports the end.
        startThread();
        return;
    }
    // Stopped or paused: the service isn't running for it, so the queue can be rebuilt here.
    // Leaves the source stopped with the new audio queued.
    const bool wasPaused = bPaused;
    primeStream(frame);
    bPaused = wasPaused;
}

void OpenALSoundPlayer::setPositionMS(int ms)
{
    if (totalFrames <= 0 || samplerate <= 0) {
        return;
    }
    seekTo(static_cast<float>(double(ms) / 1000.0 * samplerate / double(totalFrames)));
}

void OpenALSoundPlayer::setLoopRegion(const LoopRegion& region)
{
    if (!renderer.setRegion(region)) {
        return;
    }
    // While playing, the service applies it at its next chunk. When stopped, refill the queue
    // now so the next play starts at the (possibly new) begin frame. A paused sample keeps its
    // place and picks the change up when it resumes.
    if (bLoadedOk && hasVoice() && !isThreadRunning()) {
        ALint state = AL_STOPPED;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PAUSED && state != AL_PLAYING) {
            waitForThread();
            primeStream();
        }
    }
}

void OpenALSoundPlayer::setLoop(bool loop)
{
    LoopRegion r = getLoopRegion();
    r.loop = loop;
    setLoopRegion(r);
}

//------------------------------------------------------------
// UI tick
//------------------------------------------------------------
void OpenALSoundPlayer::update()
{
    if (!hasVoice()) {
        return;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    const int64_t now = steadyMs();
    if (isThreadRunning() || state == AL_PLAYING || state == AL_PAUSED || pendingStartNs.load() != 0) {
        lastActiveMs = now;
    } else if (g_voiceCount > kEagerVoices && now - lastActiveMs > kIdleReleaseMs) {
        // Over the voice budget: give back a voice nobody has used for a while.
        releaseVoice();
        return;
    }

    if (bLoadedOk && !streamPrimed && !isThreadRunning() && state == AL_STOPPED) {
        waitForThread();
        primeStream();
    }

    // Only when a send level changed: this runs for every player on every UI tick.
    if (sendsDirty) {
        applySends();
    }
}

//------------------------------------------------------------
// Levels, pan and sends
//------------------------------------------------------------
void OpenALSoundPlayer::setVolume(float vol)
{
    volume = vol;
    if (hasVoice()) {
        alSourcef(source, AL_MAX_GAIN, 6.0f);
        alSourcef(source, AL_GAIN, vol);
    }
}

void OpenALSoundPlayer::setSpeed(float spd)
{
    speed = spd;
    if (hasVoice()) {
        alSourcef(source, AL_PITCH, spd);
    }
}

void OpenALSoundPlayer::setSend(int bus, float send)
{
    if (bus >= 0 && bus < kSendCount && sends[bus] != send) {
        sends[bus] = send;
        sendsDirty = true;
    }
}

void OpenALSoundPlayer::applySends()
{
    sendsDirty = false;
    if (!hasVoice()) {
        return;
    }
    for (int bus = 0; bus < kSendCount; ++bus) {
        if (filters[bus] == 0 || !AudioDevice::sendAvailable(bus)) {
            continue;
        }
        AudioDevice::setSendFilterGain(filters[bus], sends[bus]);
        alSource3i(source, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(AudioDevice::effectSlot(bus)), bus,
                   static_cast<ALint>(filters[bus]));
    }
}

void OpenALSoundPlayer::setStereoWidth(float width)
{
    stereoWidth = std::clamp(width, 0.0f, 1.0f);
    if (channels == 2) {
        applyStereoImage();
    }
}

void OpenALSoundPlayer::setPan(float p)
{
    pan = std::clamp(p, -1.0f, 1.0f);
    if (!canPan()) {
        return;
    }
    if (channels == 1) {
        if (hasVoice()) {
            const float pos[3] = { pan, 0.0f, -std::sqrt(1.0f - pan * pan) };
            alSourcefv(source, AL_POSITION, pos);
        }
    } else {
        applyStereoImage();
    }
}

void OpenALSoundPlayer::applyStereoImage()
{
    if (channels != 2) {
        return;
    }
    if (AudioDevice::stereoAngles()) {
        // Angles are counter-clockwise (left positive), radians. The pan turns the pair towards
        // the side, the same place a mono sound at this pan sits; the width sets how far each
        // channel is from that centre (30° at full width).
        if (hasVoice()) {
            const float centre = -std::asin(std::clamp(pan, -1.0f, 1.0f));
            const float half = stereoWidth * 0.5235987755982988f;
            const ALfloat angles[2] = { centre + half, centre - half };
            alSourcefv(source, FEEDRA_AL_STEREO_ANGLES, angles);
        }
        fallbackPan.store(0.0f);
        fallbackWidth.store(1.0f);
    } else {
        // No extension: the renderer applies it to each chunk as it is prepared.
        fallbackPan.store(pan);
        fallbackWidth.store(stereoWidth);
    }
}

void OpenALSoundPlayer::applyStereoFallback(float* data, int frames)
{
    const float p = fallbackPan.load();
    const float w = fallbackWidth.load();
    if (channels != 2 || (p == 0.0f && w >= 1.0f)) {
        return;
    }
    // Width: scale the side signal. Pan: move the far channel into the near one at constant
    // power, so nothing is dropped at full pan.
    const float a = std::fabs(p) * 1.5707963267948966f;
    const float keep = std::cos(a);
    const float fold = std::sin(a);
    for (int f = 0; f < frames; ++f) {
        float* s = data + f * 2;
        const float mid = 0.5f * (s[0] + s[1]);
        const float side = 0.5f * (s[0] - s[1]) * w;
        float l = mid + side;
        float r = mid - side;
        if (p > 0.0f) {
            r += l * fold;
            l *= keep;
        } else if (p < 0.0f) {
            l += r * fold;
            r *= keep;
        }
        s[0] = l;
        s[1] = r;
    }
}
