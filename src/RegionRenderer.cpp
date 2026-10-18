#include "RegionRenderer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace {
constexpr double kHalfPi = 1.57079632679489661923;
}

void RegionRenderer::effectiveRegion(const LoopRegion& r, int rate, int64_t total,
                                     int64_t& S, int64_t& E, int64_t& xf)
{
    const int64_t fileEnd = total > 0 ? total : (INT64_MAX / 4);
    S = r.start > 0.0 ? static_cast<int64_t>(std::llround(r.start * rate)) : 0;
    E = r.end > 0.0 ? static_cast<int64_t>(std::llround(r.end * rate)) : fileEnd;
    E = std::min(E, fileEnd);
    S = std::clamp<int64_t>(S, 0, std::max<int64_t>(0, E - 1));
    // Guard against degenerate regions; the Waveform tab keeps loops far longer than this.
    const int64_t minLen = std::max<int64_t>(64, rate / 100);
    if (E - S < minLen) {
        S = 0;
        E = fileEnd;
    }
    const double xfMs = std::clamp(r.crossfadeMs, 0.0, LoopRegion::kMaxCrossfadeMs);
    xf = static_cast<int64_t>(std::llround(xfMs / 1000.0 * rate));
    if (E < INT64_MAX / 8) {
        xf = std::min(xf, (E - S) / 2);
    }
    xf = std::max<int64_t>(0, xf);
}

void RegionRenderer::setFormat(int channels, int rate, int64_t totalFrames)
{
    m_channels = channels;
    m_rate = rate;
    m_total = totalFrames;
    m_ended = false;
    m_xfPos = -1;
    m_outCounter = 0;
    m_fadeInLen = m_fadeInDone = 0;
    resetApplied();
}

bool RegionRenderer::setRegion(const LoopRegion& region)
{
    std::lock_guard<std::mutex> lock(m_regionMutex);
    if (m_requested == region) {
        return false;
    }
    m_requested = region;
    m_version.fetch_add(1);
    return true;
}

LoopRegion RegionRenderer::region() const
{
    std::lock_guard<std::mutex> lock(m_regionMutex);
    return m_requested;
}

int64_t RegionRenderer::beginFrame() const
{
    const LoopRegion r = region();
    if (r.playFromStart || m_rate <= 0) {
        return 0;
    }
    int64_t S = 0, E = 0, xf = 0;
    effectiveRegion(r, m_rate, m_total, S, E, xf);
    return S;
}

void RegionRenderer::applyPending()
{
    applyPendingRegion();
}

int64_t RegionRenderer::startRun(int64_t startFrame, bool isSeek)
{
    m_xfPos = -1;
    m_ended = false;
    m_outCounter = 0;
    applyPendingRegion();

    const int64_t begin = m_fromStart ? 0 : m_S;
    int64_t start = startFrame < 0 ? begin : startFrame;
    if (!m_fromStart) {
        start = std::max(start, m_S);
    }
    if (stopFrame() < INT64_MAX / 8) {
        start = std::min(start, std::max<int64_t>(0, stopFrame() - 1));
    }
    start = std::max<int64_t>(0, start);

    // Fade in when starting mid-waveform: over the crossfade at the loop start, briefly after a seek.
    m_fadeInDone = 0;
    if (start <= 0) {
        m_fadeInLen = 0;
    } else if (!isSeek && start == m_S) {
        m_fadeInLen = m_edgeFade;
    } else {
        m_fadeInLen = std::max<int64_t>(16, m_rate / 200);
    }
    if (m_src) {
        m_src->seek(start);
    }
    return start;
}

bool RegionRenderer::canUseInitial(int64_t framesAlready) const
{
    // Only from the top, and only when nothing in those frames needs the region (a fade, a
    // wrap or a stop).
    const int64_t stop = stopFrame();
    const int64_t untouched = m_loop ? m_E - m_xf
                                     : (m_total > 0 && stop < m_total ? stop - m_edgeFade : stop);
    return m_fromStart && framesAlready > 0 && framesAlready <= untouched;
}

void RegionRenderer::startRunAfterInitial(int64_t framesAlready)
{
    m_xfPos = -1;
    m_ended = false;
    m_fadeInLen = m_fadeInDone = 0;
    m_outCounter = framesAlready;
}

void RegionRenderer::restartAtBegin()
{
    m_xfPos = -1;
    m_ended = false;
    applyPendingRegion();
    const int64_t begin = m_fromStart ? 0 : m_S;
    if (m_src) {
        m_src->seek(begin);
    }
    m_fadeInDone = 0;
    m_fadeInLen = begin > 0 ? m_edgeFade : 0;
}

int64_t RegionRenderer::stopFrame() const
{
    if (m_loop || !m_toEnd) {
        return m_E;
    }
    return m_total > 0 ? m_total : (INT64_MAX / 4);
}

void RegionRenderer::computeEffectiveRegion()
{
    effectiveRegion(m_active, m_rate, m_total, m_S, m_E, m_xf);
    // Starting at S and stopping at a moved E only need a click-free edge, whatever the loop
    // crossfade is: 10 ms, or less on a very short region.
    m_edgeFade = std::max<int64_t>(16, m_rate / 100);
    if (m_E < INT64_MAX / 8) {
        m_edgeFade = std::min(m_edgeFade, std::max<int64_t>(1, (m_E - m_S) / 2));
    }
    m_loop = m_active.loop;
    m_toEnd = m_active.playToEnd;
    m_fromStart = m_active.playFromStart;
}

void RegionRenderer::applyPendingRegion()
{
    if (m_xfPos >= 0) {
        return; // finish the crossfade in progress with the settings it started with
    }
    if (m_version.load() == m_appliedVersion.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_regionMutex);
        m_active = m_requested;
        m_appliedVersion.store(m_version.load());
    }
    computeEffectiveRegion();
    m_headCacheValid = false;
}

void RegionRenderer::ensureHeadCache()
{
    if (m_headCacheValid) {
        return;
    }
    const size_t samples = static_cast<size_t>(std::max<int64_t>(0, m_xf)) * static_cast<size_t>(m_channels);
    m_headCache.assign(samples, 0.0f);
    if (m_xf > 0) {
        // One seek there and back, once per settings change; every later wrap reuses it.
        const int64_t back = m_src->position();
        m_src->seek(m_S);
        m_src->read(m_headCache.data(), m_xf);
        m_src->seek(back);
    }
    m_headCacheValid = true;
}

void RegionRenderer::addSegment(ChunkRecord& rec, int64_t out, int64_t file, int64_t frames)
{
    if (frames <= 0) {
        return;
    }
    if (rec.segCount > 0) {
        Segment& last = rec.seg[rec.segCount - 1];
        if (last.out + last.frames == out && last.file + last.frames == file) {
            last.frames += frames;
            return;
        }
    }
    if (rec.segCount == kMaxSegments) {
        // More wraps than fit in one chunk (tiny loop, long chunk): extend the last one.
        rec.seg[kMaxSegments - 1].frames += frames;
        return;
    }
    rec.seg[rec.segCount++] = Segment{ out, file, frames };
}

void RegionRenderer::applyFadeIn(float* data, int64_t frames)
{
    const int ch = m_channels;
    for (int64_t f = 0; f < frames && m_fadeInDone < m_fadeInLen; ++f, ++m_fadeInDone) {
        const float g = float(std::sin((double(m_fadeInDone) + 0.5) / double(m_fadeInLen) * kHalfPi));
        for (int c = 0; c < ch; ++c) {
            data[f * ch + c] *= g;
        }
    }
}

int RegionRenderer::render(ChunkRecord& rec, int want)
{
    rec = ChunkRecord{};
    rec.outStart = m_outCounter;
    const int ch = m_channels;
    if (ch <= 0) {
        return 0;
    }
    if (m_work.size() < static_cast<size_t>(want) * static_cast<size_t>(ch)) {
        m_work.resize(static_cast<size_t>(want) * static_cast<size_t>(ch));
    }
    applyPendingRegion();

    const int64_t shortFade = std::max<int64_t>(16, m_rate / 500); // 2 ms
    // Softens an unavoidable hard cut at the end of what has been rendered so far.
    auto fadeOutTail = [&](int out) {
        const int64_t n = std::min<int64_t>(out, shortFade);
        float* p = m_work.data() + static_cast<size_t>(out - n) * static_cast<size_t>(ch);
        for (int64_t f = 0; f < n; ++f) {
            const float g = float(std::cos((double(f) + 0.5) / double(n) * kHalfPi));
            for (int c = 0; c < ch; ++c) {
                p[f * ch + c] *= g;
            }
        }
    };

    int out = 0;
    for (int guard = 0; out < want && !m_ended && guard < 64; ++guard) {
        const int64_t S = m_S;
        // The loop end while looping (or finishing a crossfade), otherwise where it stops.
        const int64_t E = m_xfPos >= 0 ? m_E : stopFrame();
        const int64_t pos = m_src->position();
        float* dst = m_work.data() + static_cast<size_t>(out) * static_cast<size_t>(ch);

        if (m_xfPos >= 0) {
            // Inside the loop crossfade: the tail [E - xf, E) blends into the head [S, S + xf).
            ensureHeadCache();
            const int64_t n = std::min<int64_t>(want - out, m_xf - m_xfPos);
            const int64_t got = m_src->read(dst, n);
            if (got < n) {
                std::fill(dst + got * ch, dst + n * ch, 0.0f);
            }
            for (int64_t i = 0; i < n; ++i) {
                const double t = (double(m_xfPos + i) + 0.5) / double(m_xf) * kHalfPi;
                const float gOut = float(std::cos(t));
                const float gIn = float(std::sin(t));
                const float* head = m_headCache.data() + static_cast<size_t>(m_xfPos + i) * static_cast<size_t>(ch);
                for (int c = 0; c < ch; ++c) {
                    dst[i * ch + c] = dst[i * ch + c] * gOut + head[c] * gIn;
                }
            }
            // The playhead shows the tail until the midpoint of the crossfade, then the head.
            const int64_t half = m_xf / 2;
            const int64_t tailPart = std::clamp<int64_t>(half - m_xfPos, 0, n);
            addSegment(rec, rec.outStart + out, E - m_xf + m_xfPos, tailPart);
            addSegment(rec, rec.outStart + out + tailPart, S + m_xfPos + tailPart, n - tailPart);
            applyFadeIn(dst, n);
            m_xfPos += n;
            out += static_cast<int>(n);
            if (m_xfPos >= m_xf) {
                m_xfPos = -1;
                m_src->seek(S + m_xf);
                applyPendingRegion();
            }
            continue;
        }

        if (pos >= E) {
            // Reached E without a crossfade: a loop with no crossfade set, a region moved to
            // before the cursor, or a file that ended before its reported length.
            // Arriving exactly at E is a clean stop (any fade-out has been applied already), and a
            // clean wrap when looping with no crossfade. Anything else is a cut that needs softening.
            const bool clean = pos == E && (!m_loop || m_xf == 0);
            if (!clean && out > 0) {
                fadeOutTail(out);
            }
            if (m_loop) {
                m_src->seek(S);
                if (!clean) {
                    m_fadeInLen = shortFade;
                    m_fadeInDone = 0;
                }
                continue;
            }
            m_ended = true;
            break;
        }

        int64_t limit = E;
        if (m_loop && m_xf > 0) {
            const int64_t wrapAt = E - m_xf;
            if (pos >= wrapAt) {
                m_xfPos = pos - wrapAt;
                continue;
            }
            limit = wrapAt;
        }
        const int64_t n = std::min<int64_t>(want - out, limit - pos);
        const int64_t got = m_src->read(dst, n);
        if (got > 0) {
            // Fade-out before an end point that was moved in from the end of the file.
            const int64_t fadeOut = (!m_loop && m_total > 0 && E < m_total) ? m_edgeFade : 0;
            if (fadeOut > 0) {
                const int64_t foStart = E - fadeOut;
                for (int64_t f = std::max(pos, foStart); f < pos + got; ++f) {
                    const float g = float(std::cos((double(f - foStart) + 0.5) / double(fadeOut) * kHalfPi));
                    float* p = dst + (f - pos) * ch;
                    for (int c = 0; c < ch; ++c) {
                        p[c] *= g;
                    }
                }
            }
            applyFadeIn(dst, got);
            addSegment(rec, rec.outStart + out, pos, got);
            out += static_cast<int>(got);
        }
        if (got < n) {
            // The file ended before E.
            if (m_loop && (got > 0 || pos > S)) {
                if (out > 0) {
                    fadeOutTail(out);
                }
                m_src->seek(S);
                m_fadeInLen = shortFade;
                m_fadeInDone = 0;
                continue;
            }
            m_ended = true;
        }
    }

    m_outCounter += out;
    rec.frames = out;
    if (m_ended) {
        rec.toEnd = 0;
    } else if (m_loop || m_xfPos >= 0 || stopFrame() >= INT64_MAX / 8) {
        rec.toEnd = -1;
    } else {
        rec.toEnd = std::max<int64_t>(0, stopFrame() - m_src->position());
    }
    return out;
}

