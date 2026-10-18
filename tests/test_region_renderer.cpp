// Tests for RegionRenderer: no OpenAL, no Qt. The source is a ramp, so every output sample
// says which file frame it came from (value = frame / kTotal).

#include "RegionRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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

constexpr int kRate = 1000;      // 1 frame = 1 ms, so seconds map to round frame numbers
constexpr int64_t kTotal = 5000; // a 5 s mono file
constexpr int kChunk = 256;

double frameValue(int64_t frame) { return double(frame) / double(kTotal); }

class Ramp final : public FrameSource {
public:
    int64_t read(float* dst, int64_t frames) override
    {
        const int64_t n = std::clamp<int64_t>(kTotal - m_pos, 0, frames);
        for (int64_t i = 0; i < n; ++i) {
            dst[i] = float(frameValue(m_pos + i));
        }
        m_pos += n;
        return n;
    }
    int64_t seek(int64_t frame) override
    {
        m_pos = std::clamp<int64_t>(frame, 0, kTotal);
        return m_pos;
    }
    int64_t position() const override { return m_pos; }

private:
    int64_t m_pos = 0;
};

struct Run {
    std::vector<float> out;
    std::vector<RegionRenderer::ChunkRecord> records;
    bool ended = false;
};

Run render(RegionRenderer& r, int maxChunks)
{
    Run run;
    for (int c = 0; c < maxChunks && !r.ended(); ++c) {
        RegionRenderer::ChunkRecord rec;
        const int n = r.render(rec, kChunk);
        if (n <= 0) {
            break;
        }
        run.out.insert(run.out.end(), r.data(), r.data() + n);
        run.records.push_back(rec);
    }
    run.ended = r.ended();
    return run;
}

// The file frame a record says output frame `out` came from.
int64_t mappedFrame(const Run& run, int64_t out)
{
    for (const auto& rec : run.records) {
        if (out < rec.outStart || out >= rec.outStart + rec.frames) {
            continue;
        }
        for (int s = 0; s < rec.segCount; ++s) {
            const auto& seg = rec.seg[s];
            if (out >= seg.out && out < seg.out + seg.frames) {
                return seg.file + (out - seg.out);
            }
        }
    }
    return -1;
}

void testPlayThrough()
{
    std::printf("play through, no region\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    r.startRun(-1, false);
    const Run run = render(r, 1000);
    CHECK(run.ended, "didn't end");
    CHECK(static_cast<int64_t>(run.out.size()) == kTotal, "rendered %zu frames, want %lld", run.out.size(), (long long)kTotal);
    bool exact = true;
    for (int64_t i = 0; i < static_cast<int64_t>(run.out.size()); ++i) {
        exact = exact && std::fabs(run.out[static_cast<size_t>(i)] - float(frameValue(i))) < 1e-6f;
    }
    CHECK(exact, "output isn't the file sample for sample");
    CHECK(run.records.back().toEnd == 0, "last record doesn't say it's the end");
    CHECK(mappedFrame(run, 1234) == 1234, "playhead map off: %lld", (long long)mappedFrame(run, 1234));
}

void testStopAtEnd()
{
    std::printf("stop at a moved end point\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    LoopRegion region;
    region.end = 2.0; // E = 2000
    region.playToEnd = false;
    r.setRegion(region);
    r.startRun(-1, false);
    const Run run = render(r, 1000);
    CHECK(run.ended, "didn't end");
    CHECK(run.out.size() == 2000, "rendered %zu frames, want 2000", run.out.size());
    // Untouched until the 10 ms fade-out, then fading to near silence.
    CHECK(std::fabs(run.out[1900] - float(frameValue(1900))) < 1e-6f, "changed before the fade-out");
    CHECK(run.out.back() < 0.2f * float(frameValue(1999)), "no fade-out at E (last %f)", double(run.out.back()));
    bool falling = true;
    for (int i = 1991; i < 2000; ++i) {
        falling = falling && run.out[static_cast<size_t>(i)] <= run.out[static_cast<size_t>(i - 1)] + 1e-6f;
    }
    CHECK(falling, "fade-out isn't falling");
}

void testPlayToEndOutro()
{
    std::printf("play past E to the end (outro)\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    LoopRegion region;
    region.end = 2.0;
    region.playToEnd = true;
    r.setRegion(region);
    r.startRun(-1, false);
    const Run run = render(r, 1000);
    CHECK(static_cast<int64_t>(run.out.size()) == kTotal, "rendered %zu, want the whole file", run.out.size());
}

void testLoopCrossfade()
{
    std::printf("loop S..E with a crossfade\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    LoopRegion region;
    region.start = 1.0; // S = 1000
    region.end = 2.0;   // E = 2000
    region.loop = true;
    region.crossfadeMs = 100; // 100 frames
    r.setRegion(region);
    r.startRun(-1, false);
    const Run run = render(r, 40); // 10240 frames: several loops
    CHECK(!run.ended, "a loop ended");
    CHECK(run.out.size() == 40u * kChunk, "rendered %zu", run.out.size());
    // First pass: 0..1900 plays as is, then [1900, 2000) blends into [1000, 1100).
    CHECK(std::fabs(run.out[1500] - float(frameValue(1500))) < 1e-6f, "first pass changed");
    const double kHalfPi = 1.57079632679489661923;
    double worst = 0.0;
    for (int i = 0; i < 100; ++i) {
        const double t = (i + 0.5) / 100.0 * kHalfPi;
        const double want = frameValue(1900 + i) * std::cos(t) + frameValue(1000 + i) * std::sin(t);
        worst = std::max(worst, std::fabs(double(run.out[static_cast<size_t>(1900 + i)]) - want));
    }
    CHECK(worst < 1e-5, "crossfade off by %g", worst);
    // After the join it carries on from S + xf. Each loop is E - S - xf = 900 frames long.
    CHECK(std::fabs(run.out[2000] - float(frameValue(1100))) < 1e-6f, "after the join: %f", double(run.out[2000]));
    CHECK(std::fabs(run.out[2000 + 900] - run.out[2000]) < 1e-6f, "second loop doesn't repeat the first");
    // The playhead shows the tail until the middle of the crossfade, then the head.
    CHECK(mappedFrame(run, 1920) == 1920, "playhead in the first half of the fade: %lld", (long long)mappedFrame(run, 1920));
    CHECK(mappedFrame(run, 1980) == 1080, "playhead in the second half of the fade: %lld", (long long)mappedFrame(run, 1980));
    for (const auto& rec : run.records) {
        CHECK(rec.toEnd == -1, "a looping record has an end");
    }
}

void testPlayFromLoopStart()
{
    std::printf("start at S (play from start off)\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    LoopRegion region;
    region.start = 1.0;
    region.playFromStart = false;
    r.setRegion(region);
    CHECK(r.beginFrame() == 1000, "begin frame %lld", (long long)r.beginFrame());
    const int64_t at = r.startRun(-1, false);
    CHECK(at == 1000, "started at %lld", (long long)at);
    const Run run = render(r, 1000);
    CHECK(static_cast<int64_t>(run.out.size()) == kTotal - 1000, "rendered %zu", run.out.size());
    CHECK(run.out[0] < float(frameValue(1000)), "no fade-in at a mid-file start");
    CHECK(std::fabs(run.out[200] - float(frameValue(1200))) < 1e-6f, "fade-in lasted too long");
    // A seek into the intro is moved to S.
    r.startRun(10, true);
    CHECK(src.position() >= 1000, "seek went into the intro: %lld", (long long)src.position());
}

void testChangeWhilePlaying()
{
    std::printf("region change mid-run\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    r.startRun(-1, false);
    Run first = render(r, 2); // 512 frames
    CHECK(!r.regionPending(), "pending before any change");
    LoopRegion region;
    region.end = 1.0;
    region.playToEnd = false;
    CHECK(r.setRegion(region), "change not taken");
    CHECK(!r.setRegion(region), "the same region counted as a change");
    CHECK(r.regionPending(), "change not pending");
    const Run rest = render(r, 1000);
    CHECK(!r.regionPending(), "change not applied");
    CHECK(first.out.size() + rest.out.size() == 1000, "stopped at %zu, want 1000", first.out.size() + rest.out.size());
}

void testSeekFadeIn()
{
    std::printf("seek fades in briefly\n");
    Ramp src;
    RegionRenderer r;
    r.setSource(&src);
    r.setFormat(1, kRate, kTotal);
    r.startRun(3000, true);
    const Run run = render(r, 1);
    CHECK(run.out[0] < float(frameValue(3000)) * 0.5f, "no fade-in after a seek");
    CHECK(std::fabs(run.out[100] - float(frameValue(3100))) < 1e-6f, "seek fade-in lasted too long");
    CHECK(mappedFrame(run, 0) == 3000, "playhead after seek: %lld", (long long)mappedFrame(run, 0));
}

void testDegenerateRegion()
{
    std::printf("tiny or inverted regions fall back to the whole file\n");
    int64_t S = 0, E = 0, xf = 0;
    LoopRegion region;
    region.start = 2.0;
    region.end = 1.0;
    RegionRenderer::effectiveRegion(region, kRate, kTotal, S, E, xf);
    CHECK(S == 0 && E == kTotal, "inverted region gave %lld..%lld", (long long)S, (long long)E);
    region.start = 1.0;
    region.end = 1.001;
    RegionRenderer::effectiveRegion(region, kRate, kTotal, S, E, xf);
    CHECK(S == 0 && E == kTotal, "1 ms region gave %lld..%lld", (long long)S, (long long)E);
    region.start = 1.0;
    region.end = 1.2;
    region.crossfadeMs = 5000;
    RegionRenderer::effectiveRegion(region, kRate, kTotal, S, E, xf);
    CHECK(xf == 100, "crossfade not kept within half the loop: %lld", (long long)xf);
}

} // namespace

int main()
{
    testPlayThrough();
    testStopAtEnd();
    testPlayToEndOutro();
    testLoopCrossfade();
    testPlayFromLoopStart();
    testChangeWhilePlaying();
    testSeekFadeIn();
    testDegenerateRegion();
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
