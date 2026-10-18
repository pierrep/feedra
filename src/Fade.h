#pragma once

#include <algorithm>
#include <cmath>

namespace Fade {

// Gain at `phase` of a fade (0 silent, 1 full): equal power, so two sounds crossing keep the
// same loudness and a fade sounds even all the way down. Scene fades and pad fades use it.
inline float equalPower(float phase)
{
    return std::sin(std::clamp(phase, 0.0f, 1.0f) * 1.57079632679f);
}

} // namespace Fade
