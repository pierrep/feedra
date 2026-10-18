// The Editor page and the bottom panel on their own, with a real pad: every control writes to
// the pad or sample it should, the selection is shared through AppConfig, and a pad deleted
// under a panel (its scene was deleted) leaves the panel safe.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "AppConfig.h"
#include "AudioDevice.h"
#include "AudioSample.h"
#include "CueOutput.h"
#include "OpenALSoundPlayer.h"
#include "Pad.h"
#include "SampleLoadQueue.h"
#include "widgets/BottomPanel.h"
#include "widgets/EditorPanel.h"
#include "widgets/FileBrowserWidget.h"
#include "widgets/SampleRowWidget.h"
#include "widgets/WaveformWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QThread>

#include <functional>

#include <cmath>
#include <cstdio>
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

void writeWav(const QString& path, double secs, int channels)
{
    SF_INFO info{};
    info.samplerate = 44100;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
    std::vector<float> buf(static_cast<size_t>(secs * 44100) * static_cast<size_t>(channels));
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = 0.3f * float(std::sin(2 * 3.14159265358979 * 220 * double(i / static_cast<size_t>(channels)) / 44100.0));
    }
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size() / static_cast<size_t>(channels)));
    sf_close(f);
}

// Loads files onto the pad the way the window does: the queue decodes, the UI thread commits.
bool loadPad(SampleLoadQueue& queue, Pad& pad, const QStringList& files)
{
    pad.loadFiles(files, true);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        queue.drain(20, [&pad](SampleLoadResult result) {
            pad.submitDecoded(result.job.sampleIndex, result.job.generation, std::move(result.audio));
        });
        QCoreApplication::processEvents();
        if (pad.isLoaded() && !pad.isLoading()) {
            return true;
        }
    }
    return false;
}

template <typename T>
T* findByText(QWidget* root, const QString& text)
{
    for (T* w : root->findChildren<T*>()) {
        if (w->text() == text) {
            return w;
        }
    }
    return nullptr;
}

QSpinBox* delayBox(QWidget* root, int which)
{
    QList<QSpinBox*> boxes;
    for (QSpinBox* box : root->findChildren<QSpinBox*>()) {
        if (box->suffix() == QStringLiteral(" s")) {
            boxes << box;
        }
    }
    return which < boxes.size() ? boxes[which] : nullptr;
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
    const QString media = QDir(QCoreApplication::applicationDirPath()).filePath("panel-media");
    QDir().mkpath(media);
    const QStringList files = { media + "/stereo.wav", media + "/mono.wav", media + "/third.wav" };
    writeWav(files[0], 1.0, 2);
    writeWav(files[1], 0.8, 1);
    writeWav(files[2], 0.6, 2);

    SampleLoadQueue queue;
    auto* pad = new Pad(&config, 0, 0);
    pad->setLoadQueue(&queue);
    CHECK(loadPad(queue, *pad, files), "pad didn't load");
    CHECK(pad->soundPlayer().player.size() == 3, "%d samples", int(pad->soundPlayer().player.size()));

    std::printf("editor\n");
    {
        EditorPanel editor(&config);
        int samplesChanged = 0;
        int loopChanged = 0;
        QObject::connect(&editor, &EditorPanel::samplesChanged, [&]() { ++samplesChanged; });
        QObject::connect(&editor, &EditorPanel::loopRegionChanged, [&](AudioSample*) { ++loopChanged; });
        editor.resize(360, 600);
        editor.show();
        editor.setPad(pad);
        editor.refresh();
        CHECK(editor.findChildren<SampleRowWidget*>().size() == 3, "rows: %d", int(editor.findChildren<SampleRowWidget*>().size()));
        CHECK(config.activeSampleIdx == 0, "selection %d", config.activeSampleIdx);
        CHECK(samplesChanged > 0, "samplesChanged not sent");

        QSlider* width = nullptr;
        for (QSlider* s : editor.findChildren<QSlider*>()) {
            if (s->maximum() == 100) {
                width = s;
            }
        }
        CHECK(width && width->isVisible(), "width hidden for a stereo sample");
        editor.selectSample(1);
        CHECK(config.activeSampleIdx == 1 && config.activeSampleId == pad->soundPlayer().player[1]->id, "selectSample");
        CHECK(width && !width->isVisible(), "width shown for a mono sample");

        QDoubleSpinBox* gain = nullptr;
        for (QDoubleSpinBox* box : editor.findChildren<QDoubleSpinBox*>()) {
            if (box->suffix() == QStringLiteral(" dB")) {
                gain = box;
            }
        }
        CHECK(gain != nullptr, "no gain box");
        if (gain) {
            gain->setValue(6.0);
            const float g = pad->soundPlayer().player[1]->getGain();
            CHECK(std::fabs(g - std::pow(10.0f, 6.0f / 20.0f)) < 0.01f, "gain %f", g);
            CHECK(std::fabs(pad->soundPlayer().player[0]->getGain() - 1.0f) < 1e-4f, "gain went to the wrong sample");
        }
        if (QCheckBox* loop = findByText<QCheckBox>(&editor, QStringLiteral("Loop"))) {
            loop->setChecked(true);
            CHECK(pad->soundPlayer().player[1]->loopRegion().loop, "loop not set on the selected sample");
            CHECK(!pad->soundPlayer().player[0]->loopRegion().loop, "loop set on the wrong sample");
            CHECK(loopChanged == 1, "loopRegionChanged %d", loopChanged);
        } else {
            CHECK(false, "no loop box");
        }
        editor.selectSample(99);
        CHECK(config.activeSampleIdx == 2, "selectSample isn't clamped: %d", config.activeSampleIdx);
        editor.tick();

        // Another pad drops the rows; refresh builds the new pad's.
        editor.setPad(nullptr);
        CHECK(editor.findChildren<SampleRowWidget*>().isEmpty(), "rows kept after the pad changed");
        editor.setPad(pad);
        editor.refresh();
        CHECK(editor.findChildren<SampleRowWidget*>().size() == 3, "rows not rebuilt");
    }

    std::printf("bottom panel\n");
    {
        BottomPanel bottom(&config);
        int collapseAsks = 0;
        bool lastAsk = false;
        int loopChanged = 0;
        QObject::connect(&bottom, &BottomPanel::collapseRequested, [&](bool c) { ++collapseAsks; lastAsk = c; });
        QObject::connect(&bottom, &BottomPanel::loopRegionChanged, [&](AudioSample*) { ++loopChanged; });
        bottom.resize(900, 220);
        bottom.show();
        bottom.setPad(pad);
        bottom.refreshPadControls();

        QSpinBox* minDelay = delayBox(&bottom, 0);
        QSpinBox* maxDelay = delayBox(&bottom, 1);
        CHECK(minDelay && maxDelay, "no delay boxes");
        if (minDelay && maxDelay) {
            minDelay->setValue(5);
            CHECK(pad->soundPlayer().getMinDelay() == 5 && pad->soundPlayer().getMaxDelay() == 5,
                  "min delay didn't push max: %d %d", pad->soundPlayer().getMinDelay(), pad->soundPlayer().getMaxDelay());
            CHECK(maxDelay->value() == 5, "max box %d", maxDelay->value());
            maxDelay->setValue(2);
            CHECK(pad->soundPlayer().getMinDelay() == 2 && pad->soundPlayer().getMaxDelay() == 2, "max delay didn't push min");
            CHECK(pad->hasDelay(), "pad has no delay");
            if (QPushButton* reset = findByText<QPushButton>(&bottom, QStringLiteral("Reset delay"))) {
                reset->click();
                CHECK(!pad->hasDelay(), "reset didn't clear the delay");
            }
        }
        QList<QSlider*> sends = bottom.findChildren<QSlider*>();
        CHECK(sends.size() == OpenALSoundPlayer::kSendCount, "%d send sliders", int(sends.size()));
        if (!sends.isEmpty()) {
            sends[0]->setValue(500);
            CHECK(std::fabs(pad->sendLevel(0) - 0.5f) < 1e-4f, "send %f", pad->sendLevel(0));
        }
        if (QCheckBox* repeat = findByText<QCheckBox>(&bottom, QStringLiteral("Repeat"))) {
            repeat->setChecked(true);
            CHECK(pad->isRepeating(), "repeat not set");
        }
        if (QCheckBox* scenePad = findByText<QCheckBox>(&bottom, QStringLiteral("Plays with scene"))) {
            scenePad->setChecked(true);
            CHECK(pad->isScenePad(), "scene pad not set");
            pad->setScenePad(false);
            bottom.syncScenePad();
            CHECK(!scenePad->isChecked(), "scene pad box didn't follow the pad");
        }

        // Tabs: the open tab's button asks to collapse, another tab switches.
        CHECK(bottom.currentTab() == BottomPanel::WaveformTab, "starts on tab %d", bottom.currentTab());
        findByText<QPushButton>(&bottom, QStringLiteral("Waveform"))->click();
        CHECK(collapseAsks == 1 && lastAsk, "open tab didn't ask to collapse");
        findByText<QPushButton>(&bottom, QStringLiteral("Pad"))->click();
        CHECK(bottom.currentTab() == BottomPanel::PadTab && collapseAsks == 1, "tab switch");
        bottom.setCollapsedState(true);
        findByText<QPushButton>(&bottom, QStringLiteral("Info"))->click();
        CHECK(bottom.currentTab() == BottomPanel::InfoTab && collapseAsks == 2 && !lastAsk, "a tab didn't open the collapsed panel");
        bottom.setCollapsedState(false);

        // The waveform edits the sample selected in the Editor.
        config.activeSampleIdx = 2;
        bottom.setCurrentTab(BottomPanel::WaveformTab);
        bottom.tick();
        auto* wave = bottom.findChild<WaveformWidget*>();
        CHECK(wave != nullptr, "no waveform");
        if (wave) {
            emit wave->loopToggled(true);
            CHECK(pad->soundPlayer().player[2]->loopRegion().loop, "waveform loop not on the selected sample");
            CHECK(!pad->soundPlayer().player[0]->loopRegion().loop, "waveform loop on the wrong sample");
            emit wave->crossfadeChanged(120);
            CHECK(std::fabs(pad->soundPlayer().player[2]->loopRegion().crossfadeMs - 120.0) < 1e-6, "crossfade");
            CHECK(loopChanged == 2, "loopRegionChanged %d", loopChanged);
        }

        // The pad goes away under both panels (its scene was deleted).
        EditorPanel editor(&config);
        editor.setPad(pad);
        editor.refresh();
        delete pad;
        pad = nullptr;
        CHECK(bottom.pad() == nullptr && editor.pad() == nullptr, "panels kept a deleted pad");
        bottom.tick();
        bottom.refreshPadControls();
        editor.tick();
        editor.refresh();
        editor.selectSample(0);
    }

    std::printf("files tab preview on the cue output\n");
    if (CueOutput::available() && CueOutput::open("No Output")) {
        FileBrowserWidget browser(&config);
        // Ticks the browser for up to `ms`, until `done` holds.
        const auto tickUntil = [&browser](int ms, const std::function<bool()>& done) {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < ms && !done()) {
                browser.tick();
                QCoreApplication::processEvents();
                QThread::msleep(10);
            }
        };
        browser.previewFile(files[1]); // 0.8 s
        tickUntil(300, []() { return false; });
        CHECK(browser.isPreviewing(), "cue preview stopped early");
        CHECK(CueOutput::status().playing, "cue output isn't playing the preview");
        tickUntil(3000, [&browser]() { return !browser.isPreviewing(); });
        CHECK(!browser.isPreviewing(), "cue preview didn't end");
        browser.previewFile(media + "/not-here.wav");
        tickUntil(2000, [&browser]() { return browser.previewFailed(); });
        CHECK(browser.previewFailed(), "a missing file didn't fail on the cue output");
        browser.previewFile(files[0]);
        tickUntil(200, []() { return false; });
        browser.stopPreview();
        tickUntil(200, []() { return false; });
        CHECK(!CueOutput::status().playing, "stopPreview didn't stop the cue output");
        CueOutput::close();
    } else {
        std::printf("  (no cue output here: skipped)\n");
    }

    queue.shutdown();
    AudioDevice::close();
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
