#include "Duck.h"

#include <algorithm>
#include <cmath>

void Duck::setHeld(Source source, bool held)
{
    if (held) {
        m_sources |= source;
    } else {
        m_sources &= ~static_cast<unsigned>(source);
    }
}

void Duck::setDepthDb(float db)
{
    m_depthDb = std::clamp(db, -60.0f, 0.0f);
}

void Duck::setTimesMs(int attackMs, int releaseMs)
{
    m_attackMs = std::clamp(attackMs, 0, 10000);
    m_releaseMs = std::clamp(releaseMs, 0, 10000);
}

float Duck::update(int64_t nowMs)
{
    const int64_t elapsed = m_lastMs < 0 ? 0 : std::max<int64_t>(0, nowMs - m_lastMs);
    m_lastMs = nowMs;
    const float target = isHeld() ? m_depthDb : 0.0f;
    if (m_levelDb != target && m_depthDb < 0.0f) {
        // Full depth in m_attackMs going down, in m_releaseMs coming back.
        const bool down = target < m_levelDb;
        const int ms = down ? m_attackMs : m_releaseMs;
        const float step = ms <= 0 ? -m_depthDb : (-m_depthDb / float(ms)) * float(elapsed); // dB
        m_levelDb = down ? std::max(target, m_levelDb - step) : std::min(target, m_levelDb + step);
    } else if (m_depthDb >= 0.0f) {
        m_levelDb = 0.0f;
    }
    return gain();
}

float Duck::gain() const
{
    return m_levelDb >= 0.0f ? 1.0f : std::pow(10.0f, m_levelDb / 20.0f);
}
