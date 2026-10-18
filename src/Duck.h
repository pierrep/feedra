#pragma once

// The talk-over duck: while anything holds it (the Space key, the header's Talk button, a MIDI
// control), every scene dips to a set depth so the GM can be heard, and comes back up when
// the last one lets go.
//
// Ramps are straight lines in dB, which sound even all the way. The gain goes into
// AppConfig::masterFade, which every pad multiplies into its volume each tick. The Files
// tab's previews don't use it, so they aren't ducked.

#include <cstdint>

class Duck
{
public:
    // Who is holding it. Each lets go on its own.
    enum Source : unsigned {
        Key = 1u << 0,
        Button = 1u << 1,
        Midi = 1u << 2,
    };

    static constexpr float kDefaultDepthDb = -12.0f;
    static constexpr int kAttackMs = 150;
    static constexpr int kReleaseMs = 700;

    void setHeld(Source source, bool held);
    bool isHeld() const { return m_sources != 0; }
    bool isHeldBy(Source source) const { return (m_sources & source) != 0; }

    // Negative dB (0 turns the duck off).
    void setDepthDb(float db);
    float depthDb() const { return m_depthDb; }

    // Moves toward the target for the time since the last call. Returns the linear gain.
    float update(int64_t nowMs);
    float gain() const;
    float levelDb() const { return m_levelDb; }
    // True while ducked or still ramping.
    bool isActive() const { return m_levelDb < 0.0f; }

private:
    unsigned m_sources = 0;
    float m_depthDb = kDefaultDepthDb;
    float m_levelDb = 0.0f;
    int64_t m_lastMs = -1;
};
