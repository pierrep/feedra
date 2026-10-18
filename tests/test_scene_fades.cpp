// Scene fades and the crossfade between two scenes, with real pads on OpenAL's null device:
// the equal-power curve, a crossfade keeping the total power, fresh starts fading in only
// in a crossfade, a reversed fade carrying on from where it was, and the outgoing scene
// stopping at the end.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "AppConfig.h"
#include "AudioDevice.h"
#include "OpenALSoundPlayer.h"
#include "Pad.h"
#include "SampleLoadQueue.h"
#include "Scene.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QWidget>

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
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
    std::vector<float> buf(static_cast<size_t>(secs * 44100) * 2, 0.2f);
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size() / 2));
    sf_close(f);
}

// What the window does every 33 ms.
void tick(SampleLoadQueue& queue, const std::vector<Scene*>& scenes)
{
    queue.drain(8, [&scenes](SampleLoadResult result) {
        for (Scene* scene : scenes) {
            if (scene->id == result.job.sceneId) {
                if (Pad* pad = scene->padAt(result.job.padId)) {
                    pad->submitDecoded(result.job.sampleIndex, result.job.generation, std::move(result.audio));
                }
            }
        }
    });
    OpenALSoundPlayer::updateAll();
    for (Scene* scene : scenes) {
        scene->update();
    }
    QCoreApplication::processEvents();
}

void runFor(int ms, SampleLoadQueue& queue, const std::vector<Scene*>& scenes, const std::function<bool()>& until = {})
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms && !(until && until())) {
        tick(queue, scenes);
        QThread::msleep(10);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!AudioDevice::initialize()) {
        std::printf("no audio device: skipped\n");
        return 77;
    }

    std::printf("curve\n");
    CHECK(Scene::fadeGain(0.0f) == 0.0f && std::fabs(Scene::fadeGain(1.0f) - 1.0f) < 1e-6f, "ends");
    CHECK(std::fabs(Scene::fadeGain(0.5f) - std::sqrt(0.5f)) < 1e-5f, "middle %f", Scene::fadeGain(0.5f));
    for (float p = 0.0f; p <= 1.0f; p += 0.05f) {
        const float in = Scene::fadeGain(p);
        const float out = Scene::fadeGain(1.0f - p);
        CHECK(std::fabs(in * in + out * out - 1.0f) < 1e-4f, "power at %f: %f", p, in * in + out * out);
    }
    CHECK(Scene::fadeGain(-1.0f) == 0.0f && Scene::fadeGain(2.0f) == Scene::fadeGain(1.0f), "clamped");

    AppConfig config;
    config.setup();
    config.gridWidth = 2;
    config.gridHeight = 1;
    const QString media = QDir(QCoreApplication::applicationDirPath()).filePath("fade-media");
    QDir().mkpath(media);
    writeWav(media + "/a.wav", 8.0);
    writeWav(media + "/b.wav", 8.0);

    QWidget gridParent;
    QWidget listParent;
    SampleLoadQueue queue;
    Scene a(&config, 0, QStringLiteral("A"), &gridParent, &listParent);
    Scene b(&config, 1, QStringLiteral("B"), &gridParent, &listParent);
    const std::vector<Scene*> scenes = { &a, &b };
    Pad* padA = a.padAt(0);
    Pad* padB = b.padAt(0);
    padA->setLoadQueue(&queue);
    padB->setLoadQueue(&queue);
    padA->loadFiles({ media + "/a.wav" }, true);
    padB->loadFiles({ media + "/b.wav" }, true);
    runFor(5000, queue, scenes, [&]() { return padA->isLoaded() && padB->isLoaded() && !padA->isLoading() && !padB->isLoading(); });
    CHECK(padA->isLoaded() && padB->isLoaded(), "pads didn't load");

    std::printf("a plain play starts at full level\n");
    a.isPlaying = true;
    a.play();
    runFor(100, queue, scenes);
    CHECK(padA->isPlaying(), "A not playing");
    CHECK(std::fabs(padA->fadeVolume() - 1.0f) < 1e-6f, "fresh start faded: %f", padA->fadeVolume());
    CHECK(!a.isCrossfading(), "plain play is a crossfade");

    std::printf("crossfade A -> B over 600 ms\n");
    a.isPlaying = false;
    a.stop(600, true);
    b.isPlaying = true;
    b.play(600, true);
    CHECK(a.isCrossfading() && b.isCrossfading(), "not crossfading");
    CHECK(padB->fadeVolume() < 0.05f, "B started at %f", padB->fadeVolume());
    float worst = 0.0f;
    float lastIn = 0.0f;
    bool rising = true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 540) {
        tick(queue, scenes);
        const float out = padA->fadeVolume();
        const float in = padB->fadeVolume();
        worst = std::max(worst, std::fabs(in * in + out * out - 1.0f));
        rising = rising && in >= lastIn - 1e-6f;
        lastIn = in;
        QThread::msleep(10);
    }
    // A and B are updated a moment apart, so allow for that.
    CHECK(worst < 0.06f, "power drifted by %f during the crossfade", worst);
    CHECK(rising, "B's level went down while fading in");
    CHECK(padA->isPlaying() && padB->isPlaying(), "both should play mid-crossfade");
    runFor(400, queue, scenes);
    CHECK(!a.isCrossfading() && !b.isCrossfading(), "crossfade didn't finish");
    CHECK(!padA->isPlaying(), "A still playing after the crossfade");
    CHECK(padB->isPlaying() && std::fabs(padB->fadeVolume() - 1.0f) < 1e-6f, "B at %f", padB->fadeVolume());

    std::printf("reversed mid-fade: carries on from the level reached\n");
    a.isPlaying = true;
    a.play(1000, true);
    b.isPlaying = false;
    b.stop(1000, true);
    runFor(400, queue, scenes);
    const float reached = padA->fadeVolume();
    CHECK(reached > 0.3f && reached < 0.95f, "A at %f after 400 ms of 1000", reached);
    a.isPlaying = false;
    a.stop(1000, true);
    tick(queue, scenes);
    CHECK(std::fabs(padA->fadeVolume() - reached) < 0.08f, "jumped from %f to %f", reached, padA->fadeVolume());
    runFor(1200, queue, scenes);
    CHECK(!padA->isPlaying() && !padB->isPlaying(), "both should have stopped");

    std::printf("a plain stop is the short fade\n");
    b.isPlaying = true;
    b.play();
    runFor(100, queue, scenes);
    b.isPlaying = false;
    b.stop();
    CHECK(!b.isCrossfading(), "plain stop is a crossfade");
    runFor(Scene::kDefaultFadeMs + 200, queue, scenes);
    CHECK(!padB->isPlaying(), "B still playing after a plain stop");

    queue.shutdown();
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
