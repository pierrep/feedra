// Opens a project in the real window (offscreen), quits, and checks what was saved: every
// setting a pad and sample can have, a missing file, a relative path, an old-format pad and a
// non-ASCII file name. Then does it again with the saved file, which must come back unchanged.
//
// Uses OpenAL's null device (ALSOFT_DRIVERS=null) and Qt's offscreen platform, set by CTest.

#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

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
    for (size_t i = 0; i < buf.size() / static_cast<size_t>(channels); ++i) {
        for (int c = 0; c < channels; ++c) {
            buf[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] = 0.4f * float(std::sin(2 * 3.14159265358979 * 330 * double(i) / 44100.0));
        }
    }
    sf_writef_float(f, buf.data(), static_cast<sf_count_t>(buf.size() / static_cast<size_t>(channels)));
    sf_close(f);
}

QJsonObject sample(const QString& path, const QJsonObject& extra = {})
{
    QJsonObject s{{"path", path}, {"gain", 1.0}, {"pitch", 1.0}, {"pan", 0.0}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        s.insert(it.key(), it.value());
    }
    return s;
}

QJsonObject makeProject(const QString& media)
{
    QJsonObject rain = sample(media + "/rain.wav", {{"gain", 0.9}, {"pitch", 1.1}, {"pan", -0.3}, {"width", 0.5},
        {"loop", true}, {"loopstart", 0.2}, {"loopend", 1.2}, {"crossfade", 50.0}, {"playfromstart", false}, {"playtoend", true}});
    QJsonObject tavern{
        {"id", 0}, {"name", "Tavern"}, {"activesound", 1},
        {"0-0", QJsonObject{{"isstream", true}, {"repeat", true}, {"soundname", "Rain"}, {"volume", 0.5}, {"playrandom", true},
                    {"mindelay", 1}, {"maxdelay", 3}, {"delayon", true}, {"reverbsend", 0.25}, {"eaxsend2", 0.1},
                    {"reverbsend2", 0.3}, {"convsend2", 0.05}, {"scenepad", true},
                    {"samples", QJsonObject{{"sample-0", rain}, {"sample-1", sample(media + "/wind.wav", {{"gain", 0.6}})}}}}},
        {"0-1", QJsonObject{{"isstream", false}, {"soundname", "Wind"}, {"volume", 0.7}, {"delayon", false},
                    {"samples", QJsonObject{{"sample-0", sample(media + "/wind.wav", {{"pan", 0.5}, {"panrandom", true}})}}}}},
        {"0-5", QJsonObject{{"isstream", true}, {"soundname", "Missing one"}, {"volume", 1.0},
                    {"samples", QJsonObject{{"sample-0", sample(media + "/gone.wav")},
                                            {"sample-1", sample(media + QString::fromUtf8("/\xc3\xb6rk \xc3\xa9.wav"))}}}}},
    };
    QJsonObject forest{
        {"id", 3}, {"name", "Forest"}, {"activesound", 0},
        // The old format: the pad's repeat was "loop", and one repeating sample becomes a looping one.
        {"3-2", QJsonObject{{"isstream", true}, {"loop", true}, {"soundname", "Bell (old format)"}, {"volume", 0.3},
                    {"samples", QJsonObject{{"sample-0", sample("files/bell.wav")}}}}},
    };
    return QJsonObject{
        {"global", QJsonObject{{"mainvolume", 0.8}, {"maxscenes", 2}, {"activesceneid", 0}, {"autosave", true},
                       {"scenelimit", 14}, {"gridwidth", 6}, {"gridheight", 4}}},
        {"scene0", tavern},
        {"scene1", forest},
    };
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeJson(const QString& path, const QJsonObject& root)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(QJsonDocument(root).toJson());
}

// Opens the default settings file in a window, waits for the loads, and quits (which saves).
void runWindow(QApplication& app)
{
    MainWindow window;
    window.show();
    QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    app.exec();
}

QJsonObject padOf(const QJsonObject& root, const char* scene, const char* pad)
{
    return root.value(QLatin1String(scene)).toObject().value(QLatin1String(pad)).toObject();
}

QJsonObject sampleOf(const QJsonObject& pad, int i)
{
    return pad.value("samples").toObject().value(QString("sample-%1").arg(i)).toObject();
}

bool near(double a, double b, double tol = 1e-4)
{
    return std::fabs(a - b) <= tol;
}

QJsonObject withoutWindow(QJsonObject root)
{
    QJsonObject g = root.value("global").toObject();
    for (const QString& key : g.keys()) {
        if (key.startsWith("window")) {
            g.remove(key);
        }
    }
    root.insert("global", g);
    return root;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Feedra"));
    QApplication::setOrganizationName(QStringLiteral("Feedra"));

    const QString data = QDir(QCoreApplication::applicationDirPath()).filePath("data");
    QDir(QDir(data).filePath("settings")).removeRecursively();
    QDir(QDir(data).filePath("backups")).removeRecursively();
    QDir().mkpath(QDir(data).filePath("settings/files"));
    const QString media = QDir(data).filePath("media");
    QDir().mkpath(media);
    writeWav(media + "/rain.wav", 1.5, 2);
    writeWav(media + "/wind.wav", 1.0, 1);
    writeWav(media + QString::fromUtf8("/\xc3\xb6rk \xc3\xa9.wav"), 0.5, 2);
    writeWav(QDir(data).filePath("settings/files/bell.wav"), 0.4, 1);
    const QString settings = QDir(data).filePath("settings/settings.json");
    writeJson(settings, makeProject(media));

    std::printf("open the project and quit\n");
    runWindow(app);
    const QJsonObject saved = readJson(settings);
    CHECK(!saved.isEmpty(), "nothing saved");
    CHECK(saved.value("global").toObject().value("version").toInt() == 2, "no format version");

    const QJsonObject rain = padOf(saved, "scene0", "0-0");
    CHECK(rain.value("soundname").toString() == "Rain", "name: %s", qPrintable(rain.value("soundname").toString()));
    CHECK(near(rain.value("volume").toDouble(), 0.5, 0.01), "volume %f", rain.value("volume").toDouble());
    CHECK(rain.value("repeat").toBool() && rain.value("playrandom").toBool(), "repeat/random lost");
    CHECK(rain.value("mindelay").toInt() == 1 && rain.value("maxdelay").toInt() == 3, "delays lost");
    CHECK(near(rain.value("reverbsend").toDouble(), 0.25) && near(rain.value("eaxsend2").toDouble(), 0.1)
          && near(rain.value("reverbsend2").toDouble(), 0.3) && near(rain.value("convsend2").toDouble(), 0.05), "sends lost");
    CHECK(rain.value("scenepad").toBool(), "scene pad lost");
    const QJsonObject r0 = sampleOf(rain, 0);
    CHECK(near(r0.value("gain").toDouble(), 0.9) && near(r0.value("pitch").toDouble(), 1.1) && near(r0.value("pan").toDouble(), -0.3)
          && near(r0.value("width").toDouble(), 0.5), "sample settings lost");
    CHECK(r0.value("loop").toBool() && near(r0.value("loopstart").toDouble(), 0.2) && near(r0.value("loopend").toDouble(), 1.2)
          && near(r0.value("crossfade").toDouble(), 50.0) && !r0.value("playfromstart").toBool(true), "loop region lost");
    CHECK(near(r0.value("duration").toDouble(), 1.5, 0.01), "rain duration %f (not loaded?)", r0.value("duration").toDouble());
    CHECK(sampleOf(rain, 1).value("path").toString().endsWith("wind.wav"), "second sample lost");

    const QJsonObject wind = padOf(saved, "scene0", "0-1");
    CHECK(!wind.value("isstream").toBool(true), "memory pad became streamed");
    CHECK(wind.value("panrandom").toBool(), "random pan lost");

    const QJsonObject missing = padOf(saved, "scene0", "0-5");
    CHECK(sampleOf(missing, 0).value("path").toString().endsWith(QString::fromUtf8("\xc3\xb6rk \xc3\xa9.wav")), "non-ASCII file didn't load: %s",
          qPrintable(sampleOf(missing, 0).value("path").toString()));
    CHECK(sampleOf(missing, 1).value("path").toString().endsWith("gone.wav"), "missing sample dropped from the project");

    const QJsonObject bell = padOf(saved, "scene1", "3-2");
    CHECK(sampleOf(bell, 0).value("path").toString() == "files/bell.wav", "relative path not kept: %s",
          qPrintable(sampleOf(bell, 0).value("path").toString()));
    CHECK(bell.value("repeat").toBool() && sampleOf(bell, 0).value("loop").toBool(), "old 'loop' pad not migrated");
    CHECK(saved.value("scene1").toObject().value("name").toString() == "Forest", "scene name lost");

    std::printf("open the saved file and quit again: nothing may change\n");
    runWindow(app);
    const QJsonObject again = readJson(settings);
    CHECK(withoutWindow(again) == withoutWindow(saved), "a second round trip changed the project");

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
