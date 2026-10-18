// The live controls in the real window (offscreen): pad keys, Space for talk-over, Esc for
// Panic (and twice for an instant stop), the Talk button, and keyboard play switched off.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "MainWindow.h"
#include "MidiInput.h"
#include "Pad.h"
#include "Scene.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QSlider>
#include "widgets/SoundPadWidget.h"
#include <QPushButton>
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
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
    std::vector<float> buf(static_cast<size_t>(secs * 44100) * 2, 0.2f);
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size() / 2));
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

void key(QWidget* target, int qtKey, const QString& text, bool press = true)
{
    QKeyEvent event(press ? QEvent::KeyPress : QEvent::KeyRelease, qtKey, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &event);
}

void tap(QWidget* target, int qtKey, const QString& text)
{
    key(target, qtKey, text, true);
    key(target, qtKey, text, false);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Feedra"));

    const QString data = QDir(QCoreApplication::applicationDirPath()).filePath("data");
    QDir(QDir(data).filePath("settings")).removeRecursively();
    QDir().mkpath(QDir(data).filePath("settings"));
    const QString media = QDir(data).filePath("live-media");
    QDir().mkpath(media);
    writeWav(media + "/long.wav", 20.0);
    const auto padJson = [&](const char* name) {
        return QJsonObject{{"isstream", true}, {"soundname", name}, {"volume", 0.8}, {"delayon", false},
            {"samples", QJsonObject{{"sample-0", QJsonObject{{"path", media + "/long.wav"}}}}}};
    };
    const QJsonObject project{
        {"global", QJsonObject{{"maxscenes", 2}, {"activesceneid", 0}, {"gridwidth", 6}, {"gridheight", 4}, {"autosave", false}}},
        {"scene0", QJsonObject{{"id", 0}, {"name", "One"}, {"0-0", padJson("First")}, {"0-7", padJson("W pad")}}},
        {"scene1", QJsonObject{{"id", 1}, {"name", "Two"}, {"1-0", padJson("Other scene")}}},
    };
    QFile file(QDir(data).filePath("settings/settings.json"));
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(project).toJson());
    file.close();
    // This computer's MIDI controls, as MIDI learn would have saved them.
    const auto binding = [](const char* action, const char* type, int number, int pad = 0) {
        QJsonObject b{{"action", action}, {"type", type}, {"channel", 1}, {"number", number}};
        if (pad > 0) {
            b.insert("pad", pad);
        }
        return b;
    };
    const QJsonObject local{{"midi", QJsonObject{{"defaultnotes", true}, {"bindings", QJsonArray{
        binding("padplay", "note", 50, 8), binding("padvolume", "cc", 20, 1), binding("mastervolume", "cc", 7),
        binding("talkover", "cc", 64), binding("panic", "note", 60)}}}}};
    QFile localFile(QDir(data).filePath("settings/local.json"));
    localFile.open(QIODevice::WriteOnly);
    localFile.write(QJsonDocument(local).toJson());
    localFile.close();

    {
    MainWindow window;
    window.show();
    QPushButton* talk = window.findChild<QPushButton*>(QStringLiteral("TalkOver"));
    CHECK(talk != nullptr, "no Talk button");
    Pad* first = nullptr;
    Pad* wPad = nullptr;
    Pad* other = nullptr;
    for (Pad* pad : window.findChildren<Pad*>()) {
        if (pad->sceneId() == 0 && pad->padId() == 0) first = pad;
        if (pad->sceneId() == 0 && pad->padId() == 7) wPad = pad;
        if (pad->sceneId() == 1 && pad->padId() == 0) other = pad;
    }
    CHECK(first && wPad && other, "pads not found");
    if (!first || !wPad || !other || !talk) {
        return 1;
    }
    pump(5000, [&]() { return first->isLoaded() && wPad->isLoaded() && other->isLoaded(); });
    CHECK(first->isLoaded() && wPad->isLoaded(), "pads didn't load");

    std::printf("pad keys\n");
    tap(&window, Qt::Key_1, "1");
    pump(150);
    CHECK(first->isPlaying(), "1 didn't play the first pad");
    tap(&window, Qt::Key_W, "w");
    pump(150);
    CHECK(wPad->isPlaying(), "W didn't play pad 8");
    tap(&window, Qt::Key_1, "1");
    pump(150);
    CHECK(!first->isPlaying() && wPad->isPlaying(), "1 again didn't pause the first pad");
    tap(&window, Qt::Key_M, "m"); // off the 6x4 grid: nothing
    pump(50);

    std::printf("talk-over\n");
    key(&window, Qt::Key_Space, " ");
    pump(300);
    CHECK(talk->property("live").toBool(), "Space didn't duck");
    key(&window, Qt::Key_Space, " ", false);
    pump(1200, [&]() { return !talk->property("live").toBool(); });
    CHECK(!talk->property("live").toBool(), "the duck didn't come back up");
    talk->click();
    pump(300);
    CHECK(talk->isChecked() && talk->property("live").toBool(), "Talk button didn't duck");
    talk->click();
    pump(1200, [&]() { return !talk->property("live").toBool(); });
    CHECK(!talk->property("live").toBool(), "Talk button didn't let go");

    std::printf("panic\n");
    other->togglePlay(); // another scene's pad, playing by itself
    pump(150);
    CHECK(other->isPlaying(), "the other scene's pad didn't start");
    tap(&window, Qt::Key_Escape, QString());
    pump(400);
    CHECK(wPad->isPlaying() && other->isPlaying(), "Panic should fade, not cut");
    pump(1500, [&]() { return !wPad->isPlaying() && !other->isPlaying(); });
    CHECK(!wPad->isPlaying() && !other->isPlaying(), "Panic didn't stop everything");

    std::printf("panic twice stops at once\n");
    tap(&window, Qt::Key_W, "w");
    pump(150);
    CHECK(wPad->isPlaying(), "W didn't play again");
    tap(&window, Qt::Key_Escape, QString());
    pump(100);
    tap(&window, Qt::Key_Escape, QString());
    pump(150);
    CHECK(!wPad->isPlaying(), "a second Esc didn't stop at once");

    std::printf("keyboard play off\n");
    QCheckBox* keyboard = nullptr;
    for (QCheckBox* box : window.findChildren<QCheckBox*>()) {
        if (box->text() == QStringLiteral("Play pads from the keyboard")) {
            keyboard = box;
        }
    }
    CHECK(keyboard && keyboard->isChecked(), "keyboard setting missing or off");
    if (keyboard) {
        keyboard->setChecked(false);
        tap(&window, Qt::Key_1, "1");
        pump(150);
        CHECK(!first->isPlaying(), "1 played with keyboard play off");
        keyboard->setChecked(true);
    }

    std::printf("midi\n");
    MidiInput* midi = window.findChild<MidiInput*>();
    CHECK(midi != nullptr, "no MIDI input");
    if (midi) {
        const auto send = [midi](int status, int data1, int data2) {
            midi->deliver(MidiMessage{ static_cast<uint8_t>(status), static_cast<uint8_t>(data1), static_cast<uint8_t>(data2) });
        };
        send(0x90, 36, 100); // default notes: pad 1
        pump(150);
        CHECK(first->isPlaying(), "note 36 didn't play pad 1");
        send(0x90, 50, 100); // learned: pad 8
        pump(150);
        CHECK(wPad->isPlaying(), "note 50 didn't play pad 8");
        QSlider* master = window.findChild<QSlider*>(QStringLiteral("MasterVolume"));
        send(0xB0, 7, 0);
        CHECK(master && master->value() == 0, "CC 7 at 0 didn't move the main volume");
        send(0xB0, 7, 127);
        CHECK(master && master->value() == master->maximum(), "CC 7 at 127");
        send(0xB0, 20, 0);
        CHECK(first->padVolume() == 0.0f, "CC 20 didn't turn pad 1 down: %f", first->padVolume());
        send(0xB0, 20, 127);
        CHECK(std::fabs(first->padVolume() - 1.0f) < 1e-4f, "CC 20 at 127: %f", first->padVolume());
        send(0xB0, 64, 127);
        pump(300);
        CHECK(talk->property("live").toBool(), "CC 64 didn't hold talk-over");
        send(0xB0, 64, 0);
        pump(1200, [&]() { return !talk->property("live").toBool(); });
        CHECK(!talk->property("live").toBool(), "CC 64 didn't let go");
        send(0x90, 60, 100); // learned: Panic
        pump(1600, [&]() { return !first->isPlaying() && !wPad->isPlaying(); });
        CHECK(!first->isPlaying() && !wPad->isPlaying(), "note 60 didn't panic");

        // Learning from a pad needs an open input: without one it says so.
        SoundPadWidget* padWidget = nullptr;
        for (SoundPadWidget* w : window.findChildren<SoundPadWidget*>()) {
            if (w->pad() == first) {
                padWidget = w;
            }
        }
        QLabel* status = window.findChild<QLabel*>(QStringLiteral("MidiStatus"));
        if (padWidget && status && !midi->isOpen()) {
            emit padWidget->midiLearnRequested(0, false);
            CHECK(status->isVisibleTo(&window) && !status->text().isEmpty(), "no word about the missing MIDI input");
        }
    }
    }

    // The controls are this computer's: still in local.json after quitting.
    QFile saved(QDir(data).filePath("settings/local.json"));
    saved.open(QIODevice::ReadOnly);
    const QJsonObject savedLocal = QJsonDocument::fromJson(saved.readAll()).object();
    CHECK(savedLocal.value("midi").toObject().value("bindings").toArray().size() == 5, "MIDI controls not kept");

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
