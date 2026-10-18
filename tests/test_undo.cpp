// Undo and redo in the real window (offscreen): Pad tab and Editor settings (a drag becoming
// one step), cut and paste, moving a pad (one step for both pads) and deleting a
// scene (put back with its pads). Settings come back without reloading; changed sample
// lists reload.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "AudioSample.h"
#include "MainWindow.h"
#include "Pad.h"
#include "Scene.h"
#include "widgets/BottomPanel.h"
#include "widgets/EditorPanel.h"
#include "widgets/SceneRowWidget.h"
#include "widgets/SoundPadWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTimer>

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

SoundPadWidget* widget(int sceneId, int padId)
{
    for (SoundPadWidget* w : g_window->findChildren<SoundPadWidget*>()) {
        if (w->pad() && w->pad()->sceneId() == sceneId && w->pad()->padId() == padId) {
            return w;
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

void undo()
{
    action(QStringLiteral("&Undo"))->trigger();
    pump(60);
}

void redo()
{
    action(QStringLiteral("&Redo"))->trigger();
    pump(60);
}

bool settled(Pad* p)
{
    return p && !p->isLoading();
}

QString nameOf(int sceneId, int padId)
{
    Pad* p = pad(sceneId, padId);
    return p ? p->soundName() : QStringLiteral("<no pad>");
}

void ctrlKey(int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::ControlModifier);
    QCoreApplication::sendEvent(g_window, &press);
    pump(60);
}

int sceneCount()
{
    return g_window->findChildren<Scene*>().size();
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Feedra"));

    const QString data = QDir(QCoreApplication::applicationDirPath()).filePath("data");
    QDir(QDir(data).filePath("settings")).removeRecursively();
    QDir().mkpath(QDir(data).filePath("settings"));
    const QString media = QDir(data).filePath("undo-media");
    QDir().mkpath(media);
    writeWav(media + "/rain.wav", 2.0);
    writeWav(media + "/wind.wav", 1.5);
    writeWav(media + "/other.wav", 1.0);
    const auto padJson = [&](const char* name, const char* file) {
        return QJsonObject{{"isstream", true}, {"soundname", name}, {"volume", 0.8},
            {"samples", QJsonObject{{"sample-0", QJsonObject{{"path", media + "/" + file}}}}}};
    };
    const QJsonObject project{
        {"global", QJsonObject{{"maxscenes", 2}, {"activesceneid", 0}, {"gridwidth", 6}, {"gridheight", 4}, {"autosave", false}}},
        {"scene0", QJsonObject{{"id", 0}, {"name", "One"}, {"0-0", padJson("Rain", "rain.wav")}, {"0-1", padJson("Wind", "wind.wav")}}},
        {"scene1", QJsonObject{{"id", 1}, {"name", "Two"}, {"1-0", padJson("Other", "other.wav")}}},
    };
    QFile file(QDir(data).filePath("settings/settings.json"));
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(project).toJson());
    file.close();

    MainWindow window;
    g_window = &window;
    window.show();
    pump(5000, [&]() { return pad(0, 0)->isLoaded() && pad(0, 1)->isLoaded() && pad(1, 0)->isLoaded(); });
    CHECK(pad(0, 0)->isLoaded() && pad(0, 1)->isLoaded(), "pads didn't load");
    CHECK(action(QStringLiteral("&Undo")) && !action(QStringLiteral("&Undo"))->isEnabled(), "nothing to undo after opening");

    BottomPanel* bottom = window.findChild<BottomPanel*>();
    QSpinBox* maxDelay = nullptr;
    int boxes = 0;
    for (QSpinBox* box : bottom->findChildren<QSpinBox*>()) {
        if (box->suffix() == QStringLiteral(" s") && ++boxes == 2) {
            maxDelay = box;
        }
    }
    CHECK(maxDelay != nullptr, "no max delay box");

    std::printf("a setting, and a drag of it, are one step\n");
    if (maxDelay) {
        maxDelay->setValue(3);
        maxDelay->setValue(5);
        maxDelay->setValue(7);
        CHECK(pad(0, 0)->soundPlayer().getMaxDelay() == 7, "max delay %d", pad(0, 0)->soundPlayer().getMaxDelay());
        const Pad* before = pad(0, 0);
        const AudioSample* sample = pad(0, 0)->soundPlayer().player[0];
        undo();
        CHECK(pad(0, 0)->soundPlayer().getMaxDelay() == 0, "undo left max delay at %d", pad(0, 0)->soundPlayer().getMaxDelay());
        CHECK(pad(0, 0) == before && pad(0, 0)->soundPlayer().player.size() == 1 && pad(0, 0)->soundPlayer().player[0] == sample,
              "undoing a setting reloaded the pad");
        CHECK(maxDelay->value() == 0, "the Pad tab didn't follow the undo");
        redo();
        CHECK(pad(0, 0)->soundPlayer().getMaxDelay() == 7, "redo: %d", pad(0, 0)->soundPlayer().getMaxDelay());
        undo();
    }

    std::printf("an Editor setting\n");
    EditorPanel* editor = window.findChild<EditorPanel*>();
    editor->setPad(pad(0, 0));
    editor->refresh();
    QDoubleSpinBox* gain = nullptr;
    for (QDoubleSpinBox* box : editor->findChildren<QDoubleSpinBox*>()) {
        if (box->suffix() == QStringLiteral(" dB")) {
            gain = box;
        }
    }
    if (gain) {
        gain->setValue(6.0);
        const float raised = pad(0, 0)->soundPlayer().player[0]->getGain();
        CHECK(raised > 1.9f, "gain %f", raised);
        undo();
        CHECK(std::fabs(pad(0, 0)->soundPlayer().player[0]->getGain() - 1.0f) < 1e-4f, "gain not undone: %f",
              pad(0, 0)->soundPlayer().player[0]->getGain());
    } else {
        CHECK(false, "no gain box");
    }

    std::printf("cut and paste\n");
    ctrlKey(Qt::Key_X); // the selected pad, 1
    CHECK(!pad(0, 0)->hasSounds(), "cut didn't clear the pad");
    undo();
    pump(3000, [&]() { return settled(pad(0, 0)) && pad(0, 0)->isLoaded(); });
    CHECK(pad(0, 0)->isLoaded() && nameOf(0, 0) == QStringLiteral("Rain"), "cut not undone: '%s'", qPrintable(nameOf(0, 0)));
    redo();
    CHECK(!pad(0, 0)->hasSounds(), "cut not redone");
    undo();
    pump(3000, [&]() { return settled(pad(0, 0)) && pad(0, 0)->isLoaded(); });
    emit widget(0, 2)->padClicked(2);
    pump(60);
    ctrlKey(Qt::Key_V);
    pump(3000, [&]() { return settled(pad(0, 2)); });
    CHECK(nameOf(0, 2) == QStringLiteral("Rain"), "paste: '%s'", qPrintable(nameOf(0, 2)));
    undo();
    CHECK(!pad(0, 2)->hasSounds(), "paste not undone");

    std::printf("moving a pad is one step\n");
    emit widget(0, 3)->padDropped(1, 3, false);
    pump(3000, [&]() { return settled(pad(0, 3)); });
    CHECK(nameOf(0, 3) == QStringLiteral("Wind") && !pad(0, 1)->hasSounds(), "move: '%s' '%s'", qPrintable(nameOf(0, 3)), qPrintable(nameOf(0, 1)));
    undo();
    pump(3000, [&]() { return settled(pad(0, 1)) && pad(0, 1)->isLoaded(); });
    CHECK(nameOf(0, 1) == QStringLiteral("Wind") && !pad(0, 3)->hasSounds(), "one undo didn't put both back: '%s' '%s'",
          qPrintable(nameOf(0, 1)), qPrintable(nameOf(0, 3)));

    std::printf("deleting a scene\n");
    Scene* two = nullptr;
    for (Scene* scene : window.findChildren<Scene*>()) {
        if (scene->id == 1) {
            two = scene;
        }
    }
    CHECK(two != nullptr && sceneCount() == 2, "scenes");
    if (two) {
        QTimer::singleShot(150, []() {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(w); box && box->isVisible()) {
                    box->button(QMessageBox::Yes)->click();
                }
            }
        });
        emit two->row()->deleteRequested(1);
        pump(100);
        CHECK(sceneCount() == 1 && !pad(1, 0), "scene not deleted");
        CHECK(action(QStringLiteral("&Undo"))->text().contains(QStringLiteral("Delete scene")), "undo text: %s",
              qPrintable(action(QStringLiteral("&Undo"))->text()));
        undo();
        pump(3000, [&]() { return pad(1, 0) && pad(1, 0)->isLoaded(); });
        CHECK(sceneCount() == 2 && pad(1, 0) && pad(1, 0)->isLoaded() && nameOf(1, 0) == QStringLiteral("Other"),
              "scene not put back with its pad: %d scenes, '%s'", sceneCount(), qPrintable(nameOf(1, 0)));
        redo();
        CHECK(sceneCount() == 1, "scene delete not redone");
        undo();
        pump(3000, [&]() { return pad(1, 0) && pad(1, 0)->isLoaded(); });
        CHECK(sceneCount() == 2, "scene not back again");
    }

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
