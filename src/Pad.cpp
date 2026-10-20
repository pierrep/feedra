#include "Pad.h"

#include "AppConfig.h"
#include "AudioDevice.h"
#include "AudioSample.h"
#include "Intensity.h"
#include "FsPath.h"
#include "SampleLoadQueue.h"
#include "VolumeDb.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QWidget>

#include <algorithm>

namespace {
// The pad volume as the slider holds it: 0.1 dB steps from -60 dB (silent) to 0 dB.
float quantiseVolume(float linear)
{
    const int step = VolumeDb::toSliderClamped(std::clamp(linear, 0.0f, 1.0f), VolumeDb::kFloorDb, VolumeDb::kUnityDb);
    return VolumeDb::toLinearMuted(VolumeDb::fromSlider(step, VolumeDb::kFloorDb));
}
} // namespace

Pad::Pad(AppConfig* config, int sceneId, int padId, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_sceneId(sceneId)
    , m_padId(padId)
    , m_player(this)
    , m_volume(quantiseVolume(0.7f))
{
    m_player.setup(config, padId);
}

Pad::~Pad()
{
    if (m_loadGeneration) {
        m_loadGeneration->store(newLoadGeneration());
    }
}

int Pad::newLoadGeneration()
{
    static std::atomic<int> counter{0};
    return counter.fetch_add(1) + 1;
}

//------------------------------------------------------------
// Settings
//------------------------------------------------------------
void Pad::setSoundName(const QString& name)
{
    if (m_name == name) {
        return;
    }
    m_name = name;
    emit nameChanged(name);
}

void Pad::setPadVolume(float value)
{
    const float v = quantiseVolume(value);
    if (v == m_volume) {
        return;
    }
    m_volume = v;
    applyVolume();
    emit volumeChanged(v);
}

bool Pad::isRepeating() const
{
    return m_player.isRepeating();
}

void Pad::setRepeating(bool repeat)
{
    m_player.setRepeat(repeat);
}

bool Pad::isLoaded() const
{
    return m_player.isLoaded();
}

bool Pad::isLoading() const
{
    return m_loadTotal > 0 && m_nextCommit < m_loadTotal;
}

bool Pad::isPlaying() const
{
    return m_player.isPlaying();
}

bool Pad::hasSounds() const
{
    return !m_player.player.empty() || isLoading() || !m_missingSamples.empty();
}

bool Pad::hasDelay() const
{
    // The delay is picked between min and max (max wins if they're crossed), so any non-zero
    // value means the pad can wait before playing.
    return m_player.hasDelay();
}

QStringList Pad::samplePaths() const
{
    QStringList out;
    for (const AudioSample* sample : m_player.player) {
        out << sample->path;
    }
    return out;
}

QString Pad::samplePath(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return {};
    }
    return m_player.player[static_cast<size_t>(index)]->path;
}

QString Pad::currentSamplePath() const
{
    if (m_player.player.empty()) {
        return {};
    }
    return samplePath(std::clamp(m_player.getCurSound(), 0, static_cast<int>(m_player.player.size()) - 1));
}

QStringList Pad::missingPaths() const
{
    QStringList out;
    for (const QJsonObject& sample : m_missingSamples) {
        out << QDir::toNativeSeparators(sample.value(QStringLiteral("path")).toString());
    }
    return out;
}

void Pad::togglePlay()
{
    if (isLoading() || !isLoaded()) {
        return;
    }
    if (m_player.isPlaying()) {
        pause();
    } else {
        play();
    }
}

void Pad::play()
{
    if (isLoading() || !isLoaded()) {
        return;
    }
    applyVolume();
    m_player.setPaused(false);
}

void Pad::pause()
{
    m_player.setPaused(true);
}

void Pad::stop()
{
    m_player.stop();
}

void Pad::updateAudio()
{
    m_player.update();
    applyVolume();
}

void Pad::setFadeVolume(float fade)
{
    m_fadeVolume = fade;
    // At once, so a pad that is about to start doesn't start at the previous level.
    applyVolume();
}

void Pad::applyVolume()
{
    const float base = m_volume * m_config->masterVolume() * m_config->masterFade() * m_fadeVolume * m_layerGain;
    if (!m_player.isLoaded() || m_player.player.empty()) {
        return;
    }
    // SoundPlayer multiplies in each sample's own gain, and also applies this to a sample
    // right before it starts, so a newly selected sample never starts at the wrong volume.
    m_player.setBaseVolume(base);
}

void Pad::setLayer(int layer)
{
    layer = std::clamp(layer, 0, static_cast<int>(Intensity::Combat));
    if (layer == m_layer) {
        return;
    }
    m_layer = layer;
    if (m_layer == Intensity::None) {
        m_layerGain = 1.0f;
        applyVolume();
    }
    emit changed(); // the pad shows its layer
}

void Pad::setLayerGain(float gain)
{
    gain = m_layer == Intensity::None ? 1.0f : std::clamp(gain, 0.0f, 1.0f);
    if (gain != m_layerGain) {
        m_layerGain = gain;
        applyVolume();
    }
}

void Pad::setSend(int bus, float send)
{
    if (bus < 0 || bus >= OpenALSoundPlayer::kSendCount) {
        return;
    }
    m_sends[bus] = send;
    m_player.setSend(bus, send);
}

float Pad::sendLevel(int bus) const
{
    return bus >= 0 && bus < OpenALSoundPlayer::kSendCount ? m_sends[bus] : 0.0f;
}

void Pad::setScenePad(bool on)
{
    on = on && hasSounds(); // only pads with sounds can be scene pads
    if (m_scenePad == on) {
        return;
    }
    m_scenePad = on;
    emit scenePadChanged();
    emit changed();
}

void Pad::setCurrentSampleLooping(bool on)
{
    m_player.setCurrentSampleLooping(on);
    emit sampleLoopChanged();
}

//------------------------------------------------------------
// Loading
//------------------------------------------------------------
void Pad::cancelLoading()
{
    m_loadGeneration->store(newLoadGeneration());
    m_incoming.clear();
    m_slots.clear();
    m_loadTotal = 0;
    m_nextCommit = 0;
    m_finishSent = false;
    m_notifyWhenDone = false;
    emit changed();
}

void Pad::enqueueSample(const LoadSlot& slot)
{
    if (slot.path.isEmpty()) {
        return;
    }
    if (m_nextCommit >= m_loadTotal) {
        m_finishSent = false;
    }
    m_slots.push_back(slot);
    const int index = m_loadTotal++;
    m_config->lastPath = QFileInfo(slot.path).absolutePath();

    if (!m_queue) {
        DecodedAudio audio = AudioCodec::decodeFile(FsPath::fromQString(slot.path), m_stream, AudioDevice::floatOutput());
        submitDecoded(index, m_loadGeneration->load(), std::move(audio));
        return;
    }

    SampleLoadJob job;
    job.sceneId = m_sceneId;
    job.padId = m_padId;
    job.sampleIndex = index;
    job.generation = m_loadGeneration->load();
    job.generationToken = m_loadGeneration;
    job.path = slot.path;
    job.stream = m_stream;
    m_queue->enqueue(job);
    emit changed();
    emit loadStateChanged();
}

void Pad::submitDecoded(int index, int generation, DecodedAudio audio)
{
    if (!m_loadGeneration || generation != m_loadGeneration->load()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_slots.size())) {
        return;
    }
    m_incoming.insert_or_assign(index, std::move(audio));
    flushIncoming();
}

bool Pad::commitDecoded(int slotIndex, DecodedAudio audio)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size()) || !audio.ok) {
        return false;
    }
    auto* sample = new AudioSample();
    sample->audioPlayer = new OpenALSoundPlayer();
    int maxId = 0;
    for (AudioSample* existing : m_player.player) {
        maxId = std::max(maxId, existing->id);
    }
    sample->id = m_player.player.empty() ? 0 : maxId + 1;
    const LoadSlot& slot = m_slots[static_cast<size_t>(slotIndex)];
    // Before the upload, so the first queue is already filled from the right place.
    sample->setLoopRegion(slot.loop);
    if (!sample->audioPlayer->uploadDecoded(std::move(audio))) {
        delete sample;
        return false;
    }
    sample->path = slot.path;
    sample->setPitch(slot.pitch);
    sample->setGain(slot.gain);
    sample->setPan(slot.pan);
    sample->setWidth(slot.width);
    if (slot.panRandom) {
        m_player.setRandomPan(true);
    }
    m_player.player.push_back(sample);
    m_config->lastPath = QFileInfo(slot.path).absolutePath();
    m_sampleRate = m_player.getSampleRate();
    m_channels = m_player.getNumChannels();
    m_player.recalculateDelay(static_cast<int>(m_player.player.size()) - 1);
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        m_player.setSend(bus, m_sends[bus]);
    }
    return true;
}

void Pad::flushIncoming()
{
    while (m_incoming.find(m_nextCommit) != m_incoming.end()) {
        DecodedAudio audio = std::move(m_incoming[m_nextCommit]);
        m_incoming.erase(m_nextCommit);
        const int slotIndex = m_nextCommit++;
        if (!commitDecoded(slotIndex, std::move(audio))) {
            LoadSlot& slot = m_slots[static_cast<size_t>(slotIndex)];
            slot.failed = true;
            if (!slot.saved.isEmpty()) {
                // Keep it in the project so saving doesn't drop it.
                m_missingSamples.push_back(slot.saved);
            }
        }
    }
    emit changed();
    emit loadStateChanged();
    if (m_loadTotal > 0 && m_nextCommit >= m_loadTotal && !m_finishSent) {
        m_finishSent = true;
        emit loadingFinished();
        if (m_notifyWhenDone) {
            m_notifyWhenDone = false;
            emit filesDropped();
        }
    }
}

std::vector<Pad::LoadSlot> Pad::sampleSpecs() const
{
    auto fromSample = [this](LoadSlot spec, const AudioSample* sample) {
        spec.pitch = sample->getPitch();
        spec.gain = sample->getGain();
        spec.pan = sample->getPan();
        spec.panRandom = m_player.isRandomPan();
        spec.width = sample->getWidth();
        spec.loop = sample->loopRegion();
        return spec;
    };
    std::vector<LoadSlot> specs;
    if (!isLoading()) {
        for (const AudioSample* sample : m_player.player) {
            LoadSlot spec;
            spec.path = sample->path;
            specs.push_back(fromSample(spec, sample));
        }
        return specs;
    }
    int playerIndex = 0;
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (m_slots[static_cast<size_t>(i)].failed) {
            continue;
        }
        LoadSlot spec = m_slots[static_cast<size_t>(i)];
        if (i < m_nextCommit && playerIndex < static_cast<int>(m_player.player.size())) {
            spec = fromSample(spec, m_player.player[static_cast<size_t>(playerIndex)]);
            ++playerIndex;
        }
        specs.push_back(spec);
    }
    return specs;
}

//------------------------------------------------------------
// Project file
//------------------------------------------------------------
void Pad::loadFromJson(const QJsonObject& root, const QString& baseDir)
{
    // Older files store pads under "scene<id>" but scene names under "scene<position>", so search every scene.
    const QString prefix = QString("%1-%2").arg(m_sceneId).arg(m_padId);
    QJsonObject pad;
    for (auto it = root.begin(); it != root.end() && pad.isEmpty(); ++it) {
        pad = it.value().toObject().value(prefix).toObject();
    }
    if (pad.isEmpty()) {
        return;
    }

    clearPad();
    m_notifyWhenDone = false;
    m_stream = pad.value(QStringLiteral("isstream")).toBool(true);
    m_player.minDelay = pad.value(QStringLiteral("mindelay")).toInt();
    m_player.maxDelay = pad.value(QStringLiteral("maxdelay")).toInt();
    m_player.bDelayEnabled = pad.value(QStringLiteral("delayon")).toBool(true);
    m_player.bRandomPlayback = pad.value(QStringLiteral("playrandom")).toBool();
    // Files before sample loops stored the pad's repeat as "loop". A pad that repeated a
    // single sample with no delay was a continuous ambience: its sample now loops seamlessly.
    const bool newFormat = pad.contains(QStringLiteral("repeat"));
    const bool oldLoop = pad.value(QStringLiteral("loop")).toBool(false);
    setRepeating(newFormat ? pad.value(QStringLiteral("repeat")).toBool() : oldLoop);
    setSoundName(pad.value(QStringLiteral("soundname")).toString());
    setPadVolume(static_cast<float>(pad.value(QStringLiteral("volume")).toDouble(0.7)));
    m_sampleRate = pad.value(QStringLiteral("samplerate")).toInt();
    m_channels = pad.value(QStringLiteral("channels")).toInt();
    // "reverbsend"/"reverbsend2" are the first EAX and first convolution sends, as before.
    m_sends[0] = static_cast<float>(pad.value(QStringLiteral("reverbsend")).toDouble());
    m_sends[1] = static_cast<float>(pad.value(QStringLiteral("eaxsend2")).toDouble());
    m_sends[2] = static_cast<float>(pad.value(QStringLiteral("reverbsend2")).toDouble());
    m_sends[3] = static_cast<float>(pad.value(QStringLiteral("convsend2")).toDouble());
    m_player.setup(m_config, m_padId);
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        m_player.setSend(bus, m_sends[bus]);
    }

    const QJsonObject samples = pad.value(QStringLiteral("samples")).toObject();
    int sampleCount = 0;
    while (!samples.value(QString("sample-%1").arg(sampleCount)).toObject().isEmpty()) {
        ++sampleCount;
    }
    const bool migrateToLoop = !newFormat && oldLoop && sampleCount == 1 && m_player.minDelay <= 0 && m_player.maxDelay <= 0;
    for (int i = 0;; ++i) {
        QJsonObject sample = samples.value(QString("sample-%1").arg(i)).toObject();
        if (sample.isEmpty()) {
            break;
        }
        QString path = sample.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        // Files inside the project folder are stored relative to the project file.
        if (QDir::isRelativePath(path) && !baseDir.isEmpty()) {
            path = QDir::cleanPath(QDir(baseDir).absoluteFilePath(path));
            sample.insert(QStringLiteral("path"), path);
        }
        LoadSlot slot;
        slot.path = path;
        slot.pitch = static_cast<float>(sample.value(QStringLiteral("pitch")).toDouble(1.0));
        slot.gain = static_cast<float>(sample.value(QStringLiteral("gain")).toDouble(1.0));
        slot.pan = static_cast<float>(sample.value(QStringLiteral("pan")).toDouble());
        slot.panRandom = sample.value(QStringLiteral("panrandom")).toBool();
        slot.width = static_cast<float>(std::clamp(sample.value(QStringLiteral("width")).toDouble(1.0), 0.0, 1.0));
        slot.loop.start = std::max(0.0, sample.value(QStringLiteral("loopstart")).toDouble(0.0));
        slot.loop.end = sample.value(QStringLiteral("loopend")).toDouble(-1.0);
        slot.loop.playFromStart = sample.value(QStringLiteral("playfromstart")).toBool(true);
        slot.loop.playToEnd = sample.value(QStringLiteral("playtoend")).toBool(true);
        slot.loop.loop = sample.value(QStringLiteral("loop")).toBool(migrateToLoop);
        slot.loop.crossfadeMs = std::clamp(sample.value(QStringLiteral("crossfade")).toDouble(10.0), 0.0, LoopRegion::kMaxCrossfadeMs);
        slot.loop.fadeInSec = std::clamp(sample.value(QStringLiteral("fadein")).toDouble(0.0), 0.0, LoopRegion::kMaxFadeSec);
        slot.loop.fadeOutSec = std::clamp(sample.value(QStringLiteral("fadeout")).toDouble(0.0), 0.0, LoopRegion::kMaxFadeSec);
        slot.saved = sample;
        enqueueSample(slot);
    }
    setScenePad(pad.value(QStringLiteral("scenepad")).toBool(false));
    setLayer(Intensity::layerFromId(pad.value(QStringLiteral("layer")).toString()));
    emit changed();
}

void Pad::saveToJson(QJsonObject& sceneObj) const
{
    // A pad whose files are all missing is still saved, so it isn't lost from the project.
    if (m_player.player.empty() && m_missingSamples.empty()) {
        return;
    }

    const QString prefix = QString("%1-%2").arg(m_sceneId).arg(m_padId);
    QJsonObject pad;
    pad.insert(QStringLiteral("isstream"), m_stream);
    pad.insert(QStringLiteral("repeat"), isRepeating());
    // Older versions read the pad's repeat from "loop"; keeps the file usable there.
    pad.insert(QStringLiteral("loop"), isRepeating());
    pad.insert(QStringLiteral("soundname"), soundName());
    pad.insert(QStringLiteral("volume"), padVolume());
    pad.insert(QStringLiteral("samplerate"), m_sampleRate);
    pad.insert(QStringLiteral("channels"), m_channels);
    pad.insert(QStringLiteral("playrandom"), m_player.bRandomPlayback);
    pad.insert(QStringLiteral("panrandom"), m_player.isRandomPan());
    pad.insert(QStringLiteral("reverbsend"), m_sends[0]);
    pad.insert(QStringLiteral("eaxsend2"), m_sends[1]);
    pad.insert(QStringLiteral("reverbsend2"), m_sends[2]);
    pad.insert(QStringLiteral("convsend2"), m_sends[3]);
    pad.insert(QStringLiteral("mindelay"), m_player.minDelay);
    pad.insert(QStringLiteral("maxdelay"), m_player.maxDelay);
    pad.insert(QStringLiteral("delayon"), m_player.isDelayEnabled());
    if (m_scenePad) {
        pad.insert(QStringLiteral("scenepad"), true);
    }
    // Written only when set, so pads that don't use it read the same as before.
    if (m_layer != Intensity::None) {
        pad.insert(QStringLiteral("layer"), Intensity::layerId(m_layer));
    }

    QJsonObject samples;
    const int count = static_cast<int>(m_player.player.size());
    for (int i = 0; i < count; ++i) {
        const AudioSample* s = m_player.player[static_cast<size_t>(i)];
        QJsonObject sample;
        sample.insert(QStringLiteral("path"), s->path);
        sample.insert(QStringLiteral("gain"), s->getGain());
        sample.insert(QStringLiteral("pitch"), s->getPitch());
        sample.insert(QStringLiteral("pan"), s->getPan());
        sample.insert(QStringLiteral("panrandom"), m_player.isRandomPan());
        // "spatialise" is no longer written: stereo pans as one source, with a width.
        sample.insert(QStringLiteral("width"), s->getWidth());
        sample.insert(QStringLiteral("duration"), s->audioPlayer->getDuration());
        const LoopRegion loop = s->loopRegion();
        sample.insert(QStringLiteral("loop"), loop.loop);
        sample.insert(QStringLiteral("playfromstart"), loop.playFromStart);
        sample.insert(QStringLiteral("playtoend"), loop.playToEnd);
        sample.insert(QStringLiteral("loopstart"), loop.start);
        sample.insert(QStringLiteral("loopend"), loop.end);
        sample.insert(QStringLiteral("crossfade"), loop.crossfadeMs);
        // The sample's own fades, written only when set.
        if (loop.fadeInSec > 0.0) {
            sample.insert(QStringLiteral("fadein"), loop.fadeInSec);
        }
        if (loop.fadeOutSec > 0.0) {
            sample.insert(QStringLiteral("fadeout"), loop.fadeOutSec);
        }
        samples.insert(QString("sample-%1").arg(i), sample);
    }
    // Samples that couldn't be loaded go back as they were, after the loaded ones.
    for (size_t j = 0; j < m_missingSamples.size(); ++j) {
        samples.insert(QString("sample-%1").arg(count + static_cast<int>(j)), m_missingSamples[j]);
    }
    pad.insert(QStringLiteral("samples"), samples);
    sceneObj.insert(prefix, pad);
}

void Pad::loadFiles(const QStringList& paths, bool clearExisting)
{
    if (paths.isEmpty()) {
        return;
    }
    if (clearExisting) {
        clearPad();
    }
    m_notifyWhenDone = true;
    if (soundName().isEmpty()) {
        setSoundName(QFileInfo(paths.front()).completeBaseName());
    }
    for (const QString& path : paths) {
        LoadSlot slot;
        slot.path = path;
        enqueueSample(slot);
    }
}

void Pad::clearPad()
{
    cancelLoading();
    m_player.stop();
    m_player.close();
    m_missingSamples.clear();
    if (m_scenePad) {
        m_scenePad = false;
        emit scenePadChanged();
    }
    m_sampleRate = 0;
    m_channels = 0;
    setSoundName(QString());
    setRepeating(false);
    setPadVolume(0.7f);
    m_player.minDelay = 0;
    m_player.maxDelay = 0;
    m_player.bDelayEnabled = true;
    m_player.bRandomPlayback = false;
    m_player.bRandomPan = false;
    for (float& send : m_sends) {
        send = 0.0f;
    }
    m_layer = Intensity::None;
    m_layerGain = 1.0f;
    emit changed(); // drops the delay border, resets the playhead
}

//------------------------------------------------------------
// Copy and paste
//------------------------------------------------------------
Pad::PadClip Pad::clip() const
{
    PadClip clip;
    clip.stream = m_stream;
    clip.sampleRate = m_sampleRate;
    clip.channels = m_channels;
    clip.repeat = isRepeating();
    clip.minDelay = m_player.minDelay;
    clip.maxDelay = m_player.maxDelay;
    clip.delayOn = m_player.isDelayEnabled();
    clip.randomPlayback = m_player.bRandomPlayback;
    clip.randomPan = m_player.isRandomPan();
    clip.name = soundName();
    clip.volume = padVolume();
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        clip.sends[bus] = m_sends[bus];
    }
    for (const LoadSlot& spec : sampleSpecs()) {
        PadClip::Sample sample;
        sample.path = spec.path;
        sample.pitch = spec.pitch;
        sample.gain = spec.gain;
        sample.pan = spec.pan;
        sample.panRandom = spec.panRandom || clip.randomPan;
        sample.width = spec.width;
        sample.loop = spec.loop;
        clip.samples.push_back(sample);
    }
    clip.missing = m_missingSamples;
    clip.scenePad = m_scenePad;
    clip.layer = m_layer;
    clip.valid = !clip.samples.empty() || !clip.missing.empty();
    return clip;
}

void Pad::pasteClip(const PadClip& clip)
{
    if (!clip.valid) {
        return;
    }
    clearPad();
    m_stream = clip.stream;
    m_sampleRate = clip.sampleRate;
    m_channels = clip.channels;
    setRepeating(clip.repeat);
    m_player.minDelay = clip.minDelay;
    m_player.maxDelay = clip.maxDelay;
    m_player.bDelayEnabled = clip.delayOn;
    m_player.bRandomPlayback = clip.randomPlayback;
    m_player.setRandomPan(clip.randomPan);
    setSoundName(clip.name);
    setPadVolume(clip.volume);
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        m_sends[bus] = clip.sends[bus];
    }
    m_missingSamples = clip.missing;
    setLayer(clip.layer);
    m_notifyWhenDone = true;
    for (const PadClip::Sample& sample : clip.samples) {
        LoadSlot slot;
        slot.path = sample.path;
        slot.pitch = sample.pitch;
        slot.gain = sample.gain;
        slot.pan = sample.pan;
        slot.panRandom = sample.panRandom;
        slot.width = sample.width;
        slot.loop = sample.loop;
        enqueueSample(slot);
    }
    setScenePad(clip.scenePad);
    emit changed();
}

bool Pad::PadClip::operator==(const PadClip& o) const
{
    if (valid != o.valid) {
        return false;
    }
    if (!valid) {
        return true; // two empty pads
    }
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        if (sends[bus] != o.sends[bus]) {
            return false;
        }
    }
    return stream == o.stream && repeat == o.repeat && delayOn == o.delayOn && minDelay == o.minDelay
        && maxDelay == o.maxDelay && randomPlayback == o.randomPlayback && randomPan == o.randomPan
        && name == o.name && volume == o.volume && samples == o.samples && missing == o.missing
        && scenePad == o.scenePad && layer == o.layer;
}

bool Pad::PadClip::sameSamples(const PadClip& o) const
{
    if (samples.size() != o.samples.size() || missing != o.missing) {
        return false;
    }
    for (size_t i = 0; i < samples.size(); ++i) {
        if (samples[i].path != o.samples[i].path) {
            return false;
        }
    }
    return true;
}

void Pad::restoreClip(const PadClip& target)
{
    if (!target.valid) {
        if (hasSounds()) {
            clearPad();
        }
        return;
    }
    const PadClip now = clip();
    if (!now.valid || isLoading() || now.stream != target.stream || !now.sameSamples(target)) {
        pasteClip(target);
        return;
    }
    // The same files: put the settings back in place.
    setRepeating(target.repeat);
    m_player.minDelay = target.minDelay;
    m_player.maxDelay = target.maxDelay;
    m_player.setDelayEnabled(target.delayOn);
    m_player.setRandomPlayback(target.randomPlayback);
    m_player.setRandomPan(target.randomPan);
    for (int i = 0; i < static_cast<int>(m_player.player.size()); ++i) {
        m_player.recalculateDelay(i);
    }
    setSoundName(target.name);
    setPadVolume(target.volume);
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        setSend(bus, target.sends[bus]);
    }
    for (size_t i = 0; i < target.samples.size() && i < m_player.player.size(); ++i) {
        AudioSample* sample = m_player.player[i];
        const PadClip::Sample& want = target.samples[i];
        sample->setPitch(want.pitch);
        sample->setGain(want.gain);
        sample->setPan(want.pan);
        sample->setWidth(want.width);
        sample->setLoopRegion(want.loop);
    }
    setScenePad(target.scenePad);
    setLayer(target.layer);
    applyVolume();
    emit sampleLoopChanged();
    emit changed();
}

void Pad::appendClip(const PadClip& clip)
{
    if (!clip.valid) {
        return;
    }
    for (const QJsonObject& missing : clip.missing) {
        m_missingSamples.push_back(missing);
    }
    m_notifyWhenDone = true;
    for (const PadClip::Sample& sample : clip.samples) {
        LoadSlot slot;
        slot.path = sample.path;
        slot.pitch = sample.pitch;
        slot.gain = sample.gain;
        slot.pan = sample.pan;
        // Random pan is a setting of the pad, so the incoming pad's doesn't carry over.
        slot.panRandom = false;
        slot.width = sample.width;
        slot.loop = sample.loop;
        enqueueSample(slot);
    }
    emit changed();
}

//------------------------------------------------------------
// Sample list
//------------------------------------------------------------
int Pad::moveSample(int from, int insertIndex)
{
    if (isLoading()) {
        return -1;
    }
    const int count = static_cast<int>(m_player.player.size());
    if (from < 0 || from >= count || insertIndex < 0 || insertIndex > count) {
        return -1;
    }
    if (insertIndex == from || insertIndex == from + 1) {
        return -1;
    }

    AudioSample* sample = m_player.player[static_cast<size_t>(from)];
    m_player.player.erase(m_player.player.begin() + from);
    const int dest = insertIndex > from ? insertIndex - 1 : insertIndex;
    m_player.player.insert(m_player.player.begin() + dest, sample);

    const auto adjust = [from, dest](int idx) {
        if (idx == from) {
            return dest;
        }
        if (from < idx && dest >= idx) {
            return idx - 1;
        }
        if (from > idx && dest <= idx) {
            return idx + 1;
        }
        return idx;
    };
    m_player.curSound = adjust(m_player.curSound);
    emit changed();
    return dest;
}

void Pad::removeSampleAt(int index)
{
    if (isLoading()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return;
    }
    m_player.stop();
    delete m_player.player[static_cast<size_t>(index)];
    m_player.player.erase(m_player.player.begin() + index);
    if (m_player.player.empty() && m_missingSamples.empty()) {
        clearPad();
    }
    emit changed();
}

bool Pad::confirmFileCount(QWidget* parent, int count)
{
    constexpr int kAskAbove = 32;
    if (count <= kAskAbove) {
        return true;
    }
    return QMessageBox::question(parent, tr("Add files"),
               tr("This adds %1 files to one pad. Go ahead?").arg(count),
               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes)
        == QMessageBox::Yes;
}
