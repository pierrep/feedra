// Per-pad fades, with real pads on OpenAL's null device: fade in and out with the
// equal-power curve, turning a fade round without a jump, pause and stop at the end of a
// fade-out, nothing when the times are 0, and the times surviving saving, copying and pasting.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "AppConfig.h"
#include "AudioDevice.h"
#include "Fade.h"
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

    std::printf("saved, copied and pasted\n");
    faded.setFadeInTime(0.25f);
    faded.setFadeOutTime(2.0f);
    QJsonObject sceneObj;
    faded.saveToJson(sceneObj);
    const QJsonObject padObj = sceneObj.value("0-2").toObject();
    CHECK(std::fabs(padObj.value("fadein").toDouble() - 0.25) < 1e-6 && padObj.value("fadeout").toDouble() == 2.0, "not saved");
    {
        Pad reloaded(&config, 0, 2);
        reloaded.setLoadQueue(&queue);
        reloaded.loadFromJson(QJsonObject{ { "scene0", sceneObj } });
        CHECK(reloaded.fadeInTime() == 0.25f && reloaded.fadeOutTime() == 2.0f, "not read back");
        Pad pasted(&config, 0, 3);
        pasted.setLoadQueue(&queue);
        pasted.pasteClip(faded.clip());
        CHECK(pasted.fadeInTime() == 0.25f && pasted.fadeOutTime() == 2.0f, "not carried by copy and paste");
        pasted.clearPad();
        CHECK(pasted.fadeOutTime() == 0.0f, "clear kept the fade times");
        runFor(300); // let their loads settle before they go
    }
    faded.setFadeInTime(0.0f);
    faded.setFadeOutTime(0.0f);
    QJsonObject plain;
    faded.saveToJson(plain);
    CHECK(!plain.value("0-2").toObject().contains("fadein") && !plain.value("0-2").toObject().contains("fadeout"),
          "unset fade times were written");
    faded.setFadeVolume(1.0f);

    std::printf("fade in\n");
    faded.setFadeInTime(0.5f);
    faded.setFadeOutTime(0.5f);
    faded.play();
    tick();
    CHECK(faded.padFadeGain() < 0.2f && faded.isPlaying(), "fade in starts at %f", faded.padFadeGain());
    runFor(250);
    CHECK(std::fabs(faded.padFadeGain() - Fade::equalPower(0.5f)) < 0.15f, "half way: %f", faded.padFadeGain());
    runFor(400);
    CHECK(faded.padFadeGain() == 1.0f && !faded.isPadFading(), "fade in didn't finish: %f", faded.padFadeGain());

    std::printf("fade out to a pause, turned round half way\n");
    faded.pause();
    runFor(250);
    const float reached = faded.padFadeGain();
    CHECK(faded.isPlaying() && reached > 0.4f && reached < 0.95f, "fading out: %f", reached);
    faded.togglePlay(); // turns it round
    tick();
    CHECK(std::fabs(faded.padFadeGain() - reached) < 0.1f, "turning round jumped from %f to %f", reached, faded.padFadeGain());
    runFor(600);
    CHECK(faded.isPlaying() && faded.padFadeGain() == 1.0f, "didn't come back up: %f", faded.padFadeGain());
    faded.togglePlay(); // pause, with the fade
    runFor(800, [&]() { return !faded.isPlaying(); });
    CHECK(!faded.isPlaying() && faded.padFadeGain() == 1.0f, "didn't pause at the end of the fade");
    CHECK(faded.soundPlayer().isResuming(), "a fade to pause stopped it instead");

    std::printf("fade out to a stop\n");
    faded.play();
    runFor(600);
    faded.stop();
    runFor(250);
    CHECK(faded.isPlaying(), "stop with a fade cut at once");
    runFor(800, [&]() { return !faded.isPlaying(); });
    CHECK(!faded.isPlaying() && !faded.soundPlayer().isResuming(), "didn't stop at the end of the fade");

    std::printf("no fade times: straight away\n");
    faded.setFadeInTime(0.0f);
    faded.setFadeOutTime(0.0f);
    faded.play();
    tick();
    CHECK(faded.padFadeGain() == 1.0f && faded.isPlaying(), "started faded");
    faded.pause();
    CHECK(!faded.isPlaying(), "paused late");

    queue.shutdown();
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
