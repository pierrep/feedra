#pragma once

// How much audio the sound card is fed ahead of time. A press can't be heard sooner than the
// output buffer in front of it empties, so this is the delay between a pad (or MIDI note) and
// the sound. Smaller is snappier, and needs a computer that can keep up: too small and the
// sound crackles, most likely with several pads and the convolution reverbs going.
//
// OpenAL Soft is told through its config file, which has to be in place before the device
// opens, so a change takes effect when Feedra next starts. The file is written next to the
// per-computer settings (alsoft.ini). Default writes nothing and leaves OpenAL as it was,
// and a config the user already points ALSOFT_CONF at is never overridden.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

namespace AudioLatency {

enum Preset { Default = 0, Low = 1, Lowest = 2, Safe = 3 };
constexpr int kPresetCount = 4;

// Frames per buffer and how many, at the device's rate (48 kHz is usual). 0: OpenAL's own.
struct Buffering {
    int periodFrames;
    int periods;
};

inline Buffering buffering(int preset)
{
    switch (preset) {
    case Low: return {256, 3};     // ~16 ms
    case Lowest: return {128, 2};  // ~5 ms
    case Safe: return {1024, 4};   // ~85 ms
    default: return {0, 0};
    }
}

inline QString id(int preset)
{
    switch (preset) {
    case Low: return QStringLiteral("low");
    case Lowest: return QStringLiteral("lowest");
    case Safe: return QStringLiteral("safe");
    default: return QStringLiteral("default");
    }
}

inline int fromId(const QString& text)
{
    for (int preset = 0; preset < kPresetCount; ++preset) {
        if (text == id(preset)) {
            return preset;
        }
    }
    return Default;
}

inline QString label(int preset)
{
    switch (preset) {
    case Low: return QCoreApplication::translate("AudioLatency", "Low (about 16 ms)");
    case Lowest: return QCoreApplication::translate("AudioLatency", "Lowest (about 5 ms)");
    case Safe: return QCoreApplication::translate("AudioLatency", "Safe (about 85 ms)");
    default: return QCoreApplication::translate("AudioLatency", "Default (OpenAL's own)");
    }
}

inline QByteArray configText(int preset)
{
    const Buffering b = buffering(preset);
    if (b.periodFrames <= 0) {
        return {};
    }
    return QByteArray("[general]\nperiod_size=") + QByteArray::number(b.periodFrames)
        + "\nperiods=" + QByteArray::number(b.periods) + "\n";
}

// Before the first OpenAL call. `dir` is where alsoft.ini goes. Returns false when it left
// OpenAL alone (the default, a config of the user's own, or a file that couldn't be written).
inline bool apply(int preset, const QString& dir)
{
    if (preset == Default || qEnvironmentVariableIsSet("ALSOFT_CONF")) {
        return false;
    }
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("alsoft.ini"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(configText(preset)) < 0) {
        return false;
    }
    file.close();
    qputenv("ALSOFT_CONF", QDir::toNativeSeparators(path).toUtf8());
    return true;
}

} // namespace AudioLatency
