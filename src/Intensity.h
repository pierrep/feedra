#pragma once

// Intensity layers: a pad can be tagged Calm, Tense or Combat, and each scene has an Intensity
// slider that crossfades between its layers. Layered pads all play together, staying in time,
// and the slider only changes which ones are heard. Pads without a layer aren't affected.
//
// The slider is shared out between the layers the scene actually uses, in order: with Calm and
// Combat only, it goes straight from one to the other, so there's never a silent stretch. Two
// neighbouring layers cross with the equal-power curve, so the level holds through the change.

#include "Fade.h"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <cmath>

namespace Intensity {

enum Layer { None = 0, Calm = 1, Tense = 2, Combat = 3 };
constexpr int kLayerCount = 3; // Calm, Tense, Combat

// Seconds the heard intensity takes to cross the whole slider, so a click on the slider (or a
// jumpy MIDI knob) becomes a short crossfade rather than a cut.
constexpr float kGlideSeconds = 1.0f;

inline QString layerName(int layer)
{
    switch (layer) {
    case Calm: return QCoreApplication::translate("Intensity", "Calm");
    case Tense: return QCoreApplication::translate("Intensity", "Tense");
    case Combat: return QCoreApplication::translate("Intensity", "Combat");
    default: return QCoreApplication::translate("Intensity", "None");
    }
}

// For the project file: "calm", "tense", "combat" (nothing for None).
inline QString layerId(int layer)
{
    switch (layer) {
    case Calm: return QStringLiteral("calm");
    case Tense: return QStringLiteral("tense");
    case Combat: return QStringLiteral("combat");
    default: return QString();
    }
}

inline int layerFromId(const QString& id)
{
    for (int layer = Calm; layer <= Combat; ++layer) {
        if (id == layerId(layer)) {
            return layer;
        }
    }
    return None;
}

// The gain of the layer at `index` (0-based, in order) of the `count` layers a scene uses, with
// the slider at `intensity` (0..1). One layer is always heard in full.
inline float layerGain(int index, int count, float intensity)
{
    if (count <= 1) {
        return 1.0f;
    }
    const float position = std::clamp(intensity, 0.0f, 1.0f) * static_cast<float>(count - 1);
    const float distance = std::fabs(position - static_cast<float>(index));
    return distance >= 1.0f ? 0.0f : Fade::equalPower(1.0f - distance);
}

// The layers set in `mask` (bit `layer`), in order, and where `layer` falls among them (-1 if
// it isn't one of them).
inline int layerCount(int mask)
{
    int count = 0;
    for (int layer = Calm; layer <= Combat; ++layer) {
        count += (mask >> layer) & 1;
    }
    return count;
}

inline int layerIndex(int mask, int layer)
{
    if (layer < Calm || layer > Combat || !((mask >> layer) & 1)) {
        return -1;
    }
    int index = 0;
    for (int below = Calm; below < layer; ++below) {
        index += (mask >> below) & 1;
    }
    return index;
}

// What the slider is at, for its readout: a layer's name, or the two it's between ("Calm–Tense").
inline QString describe(int mask, float intensity)
{
    const int count = layerCount(mask);
    if (count == 0) {
        return QString();
    }
    int layers[kLayerCount] = {};
    int n = 0;
    for (int layer = Calm; layer <= Combat; ++layer) {
        if ((mask >> layer) & 1) {
            layers[n++] = layer;
        }
    }
    if (count == 1) {
        return layerName(layers[0]);
    }
    const float position = std::clamp(intensity, 0.0f, 1.0f) * static_cast<float>(count - 1);
    const int nearest = static_cast<int>(std::lround(position));
    if (std::fabs(position - static_cast<float>(nearest)) < 0.08f) {
        return layerName(layers[nearest]);
    }
    const int below = std::min(count - 2, static_cast<int>(std::floor(position)));
    return QCoreApplication::translate("Intensity", "%1–%2").arg(layerName(layers[below]), layerName(layers[below + 1]));
}

} // namespace Intensity
