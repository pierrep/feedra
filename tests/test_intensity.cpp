// Intensity layers. The crossfade between layers on its own (Intensity.h), then in the real
// window (offscreen): layered pads start with their scene and keep playing while the header's
// slider crossfades them, with a glide and the level held through the change; unlayered pads
// are left alone; the slider follows the selected scene and hides for scenes without layers;
// a MIDI fader moves it; the Pad tab sets a pad's layer, and undo puts it back.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "ControlMap.h"
#include "Intensity.h"
#include "MainWindow.h"
#include "MidiInput.h"
#include "Pad.h"
#include "Scene.h"
#include "widgets/BottomPanel.h"
#include "widgets/SceneRowWidget.h"
#include "widgets/SoundPadWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSlider>
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

bool near(float a, float b, float tol = 1e-4f)
{
    return std::fabs(a - b) <= tol;
}

void writeWav(const QString& path, double secs)
{
    SF_INFO info{};
    info.samplerate = 44100;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
    std::vector<float> buf(static_cast<size_t>(secs * 44100), 0.1f);
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size()));
    sf_close(f);
}

void pump(int ms, const std::function<bool()>& until = {})
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms && !(until && until())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

MainWindow* g_window = nullptr;

Pad* pad(int sceneId, int padId)
{
    for (Pad* p : g_window->findChildren<Pad*>()) {
        if (p->sceneId() == sceneId && p->padId() == padId) {
            return p;
        }
    }
    return nullptr;
}

Scene* scene(int id)
{
    for (Scene* s : g_window->findChildren<Scene*>()) {
        if (s->id == id) {
            return s;
        }
    }
    return nullptr;
}

QAction* action(const QString& prefix)
{
    for (QAction* a : g_window->findChildren<QAction*>()) {
        if (a->text().startsWith(prefix)) {
            return a;
        }
    }
    return nullptr;
}

void unitChecks()
{
    std::printf("the crossfade on its own\n");
    using namespace Intensity;
    CHECK(layerGain(0, 1, 0.0f) == 1.0f && layerGain(0, 1, 1.0f) == 1.0f, "one layer isn't always full");
    CHECK(layerGain(0, 3, 0.0f) == 1.0f && layerGain(1, 3, 0.0f) == 0.0f && layerGain(2, 3, 0.0f) == 0.0f, "calm end");
    CHECK(layerGain(1, 3, 0.5f) == 1.0f && layerGain(0, 3, 0.5f) == 0.0f && layerGain(2, 3, 0.5f) == 0.0f, "tense middle");
    CHECK(layerGain(2, 3, 1.0f) == 1.0f && layerGain(1, 3, 1.0f) == 0.0f, "combat end");
    // Equal power: the two layers crossing keep the same total power all the way.
    for (float x = 0.0f; x <= 1.0f; x += 0.05f) {
        float power = 0.0f;
        for (int i = 0; i < 3; ++i) {
            power += layerGain(i, 3, x) * layerGain(i, 3, x);
        }
        CHECK(near(power, 1.0f, 1e-3f), "power %f at %f", power, x);
    }
    CHECK(near(layerGain(0, 2, 0.5f), layerGain(1, 2, 0.5f)) && near(layerGain(0, 2, 0.5f), std::sqrt(0.5f), 1e-3f),
          "two layers don't meet at -3 dB");
    const int calmCombat = (1 << Calm) | (1 << Combat);
    CHECK(layerCount(calmCombat) == 2 && layerIndex(calmCombat, Calm) == 0 && layerIndex(calmCombat, Combat) == 1
              && layerIndex(calmCombat, Tense) == -1 && layerIndex(calmCombat, None) == -1, "layer positions");
    CHECK(describe(calmCombat, 0.0f) == "Calm" && describe(calmCombat, 1.0f) == "Combat"
              && describe(calmCombat, 0.5f) == QString::fromUtf8("Calm–Combat") && describe(0, 0.5f).isEmpty(),
          "readout '%s'", qPrintable(describe(calmCombat, 0.5f)));
    CHECK(layerFromId(layerId(Tense)) == Tense && layerFromId("loud") == None && layerId(None).isEmpty(), "ids");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Feedra"));
    unitChecks();

    const QString data = QDir(QCoreApplication::applicationDirPath()).filePath("data");
    QDir(QDir(data).filePath("settings")).removeRecursively();
    QDir().mkpath(QDir(data).filePath("settings"));
    const QString media = QDir(data).filePath("intensity-media");
    QDir().mkpath(media);
    writeWav(media + "/long.wav", 8.0);
    const auto padJson = [&](const char* name, const char* layer, bool scenePad = false) {
        QJsonObject p{{"isstream", true}, {"repeat", true}, {"soundname", name}, {"volume", 1.0},
            {"samples", QJsonObject{{"sample-0", QJsonObject{{"path", media + "/long.wav"}, {"loop", true}}}}}};
        if (*layer) {
            p.insert("layer", layer);
        }
        if (scenePad) {
            p.insert("scenepad", true);
        }
        return p;
    };
    const QJsonObject project{
        {"global", QJsonObject{{"maxscenes", 2}, {"activesceneid", 0}, {"gridwidth", 6}, {"gridheight", 4}, {"autosave", false}}},
        // A scene pad (Bed), so only scene pads and layered pads start with the scene: not Other.
        {"scene0", QJsonObject{{"id", 0}, {"name", "Fight"}, {"intensity", 0.0},
            {"0-0", padJson("Calm", "calm")}, {"0-1", padJson("Combat", "combat")},
            {"0-2", padJson("Bed", "", true)}, {"0-3", padJson("Other", "")}}},
        {"scene1", QJsonObject{{"id", 1}, {"name", "Plain"}, {"1-0", padJson("Plain", "")}}},
    };
    QFile file(QDir(data).filePath("settings/settings.json"));
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(project).toJson());
    file.close();
    const QJsonObject local{{"midi", QJsonObject{{"defaultnotes", true}, {"bindings", QJsonArray{
        QJsonObject{{"action", "intensity"}, {"type", "cc"}, {"channel", 1}, {"number", 21}}}}}}};
    QFile localFile(QDir(data).filePath("settings/local.json"));
    localFile.open(QIODevice::WriteOnly);
    localFile.write(QJsonDocument(local).toJson());
    localFile.close();

    MainWindow window;
    g_window = &window;
    window.show();
    pump(5000, [&]() {
        for (int i = 0; i < 4; ++i) {
            if (!pad(0, i)->isLoaded()) {
                return false;
            }
        }
        return pad(1, 0)->isLoaded();
    });
    CHECK(pad(0, 0)->isLoaded() && pad(0, 1)->isLoaded(), "pads didn't load");
    CHECK(pad(0, 0)->layer() == Intensity::Calm && pad(0, 1)->layer() == Intensity::Combat && pad(0, 2)->layer() == 0,
          "layers not read");

    QSlider* slider = window.findChild<QSlider*>(QStringLiteral("Intensity"));
    CHECK(slider != nullptr, "no Intensity slider");
    if (!slider) {
        return 1;
    }
    pump(100);
    CHECK(slider->isVisibleTo(&window), "the slider hides for a scene with layers");

    std::printf("layered pads start with the scene and are crossfaded\n");
    emit scene(0)->row()->playPauseRequested(0);
    pump(800); // past the scene's fade-in
    CHECK(pad(0, 0)->isPlaying() && pad(0, 1)->isPlaying() && pad(0, 2)->isPlaying(), "layered or scene pads didn't start");
    CHECK(!pad(0, 3)->isPlaying(), "a plain pad started with the scene pads");
    CHECK(pad(0, 0)->layerGain() == 1.0f && pad(0, 1)->layerGain() == 0.0f, "at Calm: %f %f", pad(0, 0)->layerGain(),
          pad(0, 1)->layerGain());
    CHECK(pad(0, 2)->layerGain() == 1.0f, "an unlayered pad was changed");
    CHECK(pad(0, 1)->soundPlayer().getBaseVolume() == 0.0f && pad(0, 0)->soundPlayer().getBaseVolume() > 0.5f,
          "the layer gain isn't in the pad's volume: %f %f", pad(0, 0)->soundPlayer().getBaseVolume(),
          pad(0, 1)->soundPlayer().getBaseVolume());

    slider->setValue(1000);
    CHECK(scene(0)->intensity() == 1.0f, "slider didn't reach the scene");
    pump(450);
    const float calm = pad(0, 0)->layerGain();
    const float combat = pad(0, 1)->layerGain();
    CHECK(calm > 0.05f && calm < 0.98f && combat > 0.05f && combat < 0.98f, "no glide: %f %f", calm, combat);
    CHECK(near(calm * calm + combat * combat, 1.0f, 0.02f), "level dipped in the crossfade: %f", calm * calm + combat * combat);
    pump(1500, [&]() { return pad(0, 1)->layerGain() == 1.0f; });
    CHECK(pad(0, 0)->layerGain() == 0.0f && pad(0, 1)->layerGain() == 1.0f, "at Combat: %f %f", pad(0, 0)->layerGain(),
          pad(0, 1)->layerGain());
    CHECK(pad(0, 0)->isPlaying(), "the silent layer stopped: it must keep its place");

    std::printf("the slider follows the selected scene\n");
    emit scene(1)->row()->selected(1);
    pump(150);
    CHECK(!slider->isVisibleTo(&window), "the slider shows for a scene without layers");
    emit scene(0)->row()->selected(0);
    pump(150);
    CHECK(slider->isVisibleTo(&window) && slider->value() == 1000, "back on the layered scene: %d", slider->value());

    std::printf("a MIDI fader\n");
    MidiInput* midi = window.findChild<MidiInput*>();
    CHECK(midi != nullptr, "no MIDI input");
    if (midi) {
        midi->deliver(MidiMessage{ 0xB0, 21, 0 });
        pump(100);
        CHECK(scene(0)->intensity() == 0.0f && slider->value() == 0, "CC 21 didn't move it: %f", scene(0)->intensity());
    }
    pump(1500, [&]() { return pad(0, 0)->layerGain() == 1.0f; });
    CHECK(pad(0, 0)->layerGain() == 1.0f, "didn't glide back to Calm");

    std::printf("the Pad tab sets the layer, and undo puts it back\n");
    BottomPanel* bottom = window.findChild<BottomPanel*>();
    emit scene(0)->padWidgets[2]->padClicked(2); // Bed
    pump(100);
    QComboBox* layerBox = nullptr;
    for (QComboBox* box : bottom->findChildren<QComboBox*>()) {
        if (box->findText(QStringLiteral("Combat")) >= 0) {
            layerBox = box;
        }
    }
    CHECK(layerBox && layerBox->isEnabled() && layerBox->currentText() == "None", "no layer box for Bed");
    if (layerBox) {
        layerBox->setCurrentIndex(layerBox->findText(QStringLiteral("Tense")));
        pump(100);
        CHECK(pad(0, 2)->layer() == Intensity::Tense, "the box didn't set the layer");
        // Calm, Tense, Combat now; at 0 Tense is silent.
        CHECK(pad(0, 2)->layerGain() == 0.0f, "Tense at Calm: %f", pad(0, 2)->layerGain());
        action(QStringLiteral("&Undo"))->trigger();
        pump(100);
        CHECK(pad(0, 2)->layer() == Intensity::None && pad(0, 2)->layerGain() == 1.0f && layerBox->currentText() == "None",
              "undo left layer %d", pad(0, 2)->layer());
        action(QStringLiteral("&Redo"))->trigger();
        pump(100);
        CHECK(pad(0, 2)->layer() == Intensity::Tense, "redo");
    }

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
