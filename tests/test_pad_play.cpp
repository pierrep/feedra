// Pads on OpenAL's null device: play, pause and stop happen straight away, and a sample's own
// fade in and out survive saving, loading, copying and pasting (and old pad-level fade keys are
// ignored).
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "AppConfig.h"
#include "AudioDevice.h"
#include "AudioSample.h"
#include "OpenALSoundPlayer.h"
#include "Pad.h"
#include "SampleLoadQueue.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>
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

void writeWav(const QString& path, double secs)
{
    SF_INFO info{};
    info.samplerate = 44100;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
    std::vector<float> buf(static_cast<size_t>(secs * 44100), 0.2f);
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size()));
    sf_close(f);
}

std::vector<Pad*> g_pads;
SampleLoadQueue* g_queue = nullptr;

void tick()
{
    g_queue->drain(8, [](SampleLoadResult result) {
        for (Pad* pad : g_pads) {
            if (pad->sceneId() == result.job.sceneId && pad->padId() == result.job.padId) {
                pad->submitDecoded(result.job.sampleIndex, result.job.generation, std::move(result.audio));
            }
        }
    });
    OpenALSoundPlayer::updateAll();
    for (Pad* pad : g_pads) {
        pad->updateAudio();
    }
    QCoreApplication::processEvents();
}

void runFor(int ms, const std::function<bool()>& until = {})
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms && !(until && until())) {
        tick();
        QThread::msleep(5);
    }
}

bool loaded(Pad* pad)
{
    return pad->isLoaded() && !pad->isLoading();
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!AudioDevice::initialize()) {
        std::printf("no audio device: skipped\n");
        return 77;
    }
    AppConfig config;
    config.setup();
    const QString media = QDir(QCoreApplication::applicationDirPath()).filePath("pad-play-media");
    QDir().mkpath(media);
    writeWav(media + "/long.wav", 6.0);

    SampleLoadQueue queue;
    g_queue = &queue;
    Pad faded(&config, 0, 2);
    g_pads = { &faded };
    faded.setLoadQueue(&queue);
    faded.loadFiles({ media + "/long.wav" }, true);
    runFor(5000, [&]() { return loaded(&faded); });
    CHECK(loaded(&faded), "pad didn't load");

    std::printf("play, pause and stop are immediate\n");
    faded.play();
    tick();
    CHECK(faded.isPlaying(), "play didn't start the pad");
    faded.pause();
    CHECK(!faded.isPlaying(), "pause isn't immediate");
    faded.togglePlay();
    CHECK(faded.isPlaying(), "toggle didn't play");
    faded.togglePlay();
    CHECK(!faded.isPlaying(), "toggle didn't pause");
    faded.play();
    faded.stop();
    CHECK(!faded.isPlaying() && !faded.soundPlayer().isResuming(), "stop isn't immediate");

    std::printf("the sample's own fades: saved, copied and pasted\n");
    AudioSample* sample = faded.soundPlayer().player[0];
    LoopRegion region = sample->loopRegion();
    CHECK(region.fadeInSec == 0.0 && region.fadeOutSec == 0.0, "fades set on a new sample");
    region.fadeInSec = 0.25;
    region.fadeOutSec = 12.5; // past the 6 s sample: kept as set, the renderer limits it to the file
    sample->setLoopRegion(region);
    QJsonObject sceneObj;
    faded.saveToJson(sceneObj);
    const QJsonObject padObj = sceneObj.value("0-2").toObject();
    const QJsonObject sampleObj = padObj.value("samples").toObject().value("sample-0").toObject();
    CHECK(std::fabs(sampleObj.value("fadein").toDouble() - 0.25) < 1e-9 && sampleObj.value("fadeout").toDouble() == 12.5, "not saved");
    CHECK(!padObj.contains("fadein") && !padObj.contains("fadeout"), "written on the pad");
    {
        // Another pad id, so the loader's results find this pad and not the original.
        Pad reloaded(&config, 0, 5);
        reloaded.setLoadQueue(&queue);
        reloaded.loadFromJson(QJsonObject{ { "scene0", QJsonObject{ { "0-5", padObj } } } });
        g_pads.push_back(&reloaded);
        runFor(5000, [&]() { return loaded(&reloaded); });
        CHECK(loaded(&reloaded), "reloaded pad didn't load");
        if (loaded(&reloaded)) {
            const LoopRegion back = reloaded.soundPlayer().player[0]->loopRegion();
            CHECK(back.fadeInSec == 0.25 && back.fadeOutSec == 12.5, "not read back: %f %f", back.fadeInSec, back.fadeOutSec);
        }
        Pad pasted(&config, 0, 3);
        pasted.setLoadQueue(&queue);
        g_pads.push_back(&pasted);
        pasted.pasteClip(faded.clip());
        runFor(5000, [&]() { return loaded(&pasted); });
        CHECK(loaded(&pasted), "pasted pad didn't load");
        if (loaded(&pasted)) {
            const LoopRegion copied = pasted.soundPlayer().player[0]->loopRegion();
            CHECK(copied.fadeInSec == 0.25 && copied.fadeOutSec == 12.5, "not carried by copy and paste");
            CHECK(pasted.clip() == faded.clip(), "the copy differs from the pad");
        }
        // A fade is part of the sample, so a clip with other fades isn't the same pad (undo needs to tell).
        Pad::PadClip other = faded.clip();
        other.samples[0].loop.fadeInSec = 1.0;
        CHECK(other != faded.clip(), "clips with different fade ins compare equal");
        other = faded.clip();
        other.samples[0].loop.fadeOutSec = 1.0;
        CHECK(other != faded.clip(), "clips with different fade outs compare equal");
        g_pads.pop_back();
        g_pads.pop_back();
        runFor(300);
    }

    std::printf("pad-level fade keys from an earlier version are ignored\n");
    {
        QJsonObject old = padObj;
        old.insert("fadein", 3.0);
        old.insert("fadeout", 4.0);
        Pad legacy(&config, 0, 4);
        legacy.setLoadQueue(&queue);
        g_pads.push_back(&legacy);
        legacy.loadFromJson(QJsonObject{ { "scene0", QJsonObject{ { "0-4", old } } } });
        runFor(5000, [&]() { return loaded(&legacy); });
        QJsonObject again;
        legacy.saveToJson(again);
        CHECK(!again.value("0-4").toObject().contains("fadein") && !again.value("0-4").toObject().contains("fadeout"),
              "old pad fade keys were written back");
        g_pads.pop_back();
        runFor(300);
    }

    std::printf("a fade of zero isn't written\n");
    {
        LoopRegion none = faded.soundPlayer().player[0]->loopRegion();
        none.fadeInSec = 0.0;
        none.fadeOutSec = 0.0;
        faded.soundPlayer().player[0]->setLoopRegion(none);
        QJsonObject plain;
        faded.saveToJson(plain);
        const QJsonObject sampleJson = plain.value("0-2").toObject().value("samples").toObject().value("sample-0").toObject();
        CHECK(!sampleJson.contains("fadein") && !sampleJson.contains("fadeout"), "unset fades were written");
    }

    queue.shutdown();
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
