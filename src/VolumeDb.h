#pragma once

#include <QString>

#include <algorithm>
#include <cmath>

// Sliders move in decibels. Playback and saved projects still use linear gain.
namespace VolumeDb {

constexpr float kFloorDb = -60.0f;
constexpr float kUnityDb = 0.0f;
constexpr int kStepsPerDb = 10;

inline float toLinear(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

// The bottom detent is silence. Any step above the floor is a real gain.
inline float toLinearMuted(float db, float floorDb = kFloorDb)
{
    if (db <= floorDb) {
        return 0.0f;
    }
    return toLinear(db);
}

inline float toDb(float linear)
{
    if (linear <= 1.0e-8f) {
        return kFloorDb;
    }
    return 20.0f * std::log10(linear);
}

inline int toSlider(float db, float floorDb)
{
    return static_cast<int>(std::lround((db - floorDb) * static_cast<float>(kStepsPerDb)));
}

inline int toSliderClamped(float linear, float floorDb, float ceilDb)
{
    const float db = std::clamp(toDb(linear), floorDb, ceilDb);
    return toSlider(db, floorDb);
}

inline float fromSlider(int slider, float floorDb)
{
    return floorDb + static_cast<float>(slider) / static_cast<float>(kStepsPerDb);
}

inline int sliderSpan(float floorDb, float ceilDb)
{
    return toSlider(ceilDb, floorDb);
}

inline QString format(float db)
{
    return QString::number(static_cast<double>(db), 'f', 1) + QStringLiteral(" dB");
}

} // namespace VolumeDb
