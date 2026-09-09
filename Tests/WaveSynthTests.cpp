// ChipBoy -- the wave synth: every source, every shaper on a known input,
// the morph's ends and the determinism the exporter relies on
// (docs/COMMANDS_AND_TEMPO.md section 33).
#include "core/Bank/WaveSynth.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace chipboy;
using namespace chipboy::bank;

namespace {

/// A synth with an empty chain: the source, and nothing done to it.
Synth plain(SynthSource src)
{
    Synth s;
    s.used = true;
    s.start.source = s.end.source = src;
    s.frames = 1;
    return s;
}

/// One shaper, at one amount, over a source.
Synth withShaper(SynthSource src, SynthShaper shaper, int amount, int resonance = 0)
{
    Synth s = plain(src);
    s.chain[0] = shaper;
    s.start.amount[0] = int8_t(amount);
    s.start.resonance[0] = uint8_t(resonance);
    s.end = s.start;
    return s;
}

Frame render(const Synth& s, const Frame& drawn = Frame{}) { return synthesizeFrame(s, drawn, 0); }

/// The wave a "vertical" measure can be taken from: how far the frame swings.
int peakToPeak(const Frame& f)
{
    int lo = 15, hi = 0;
    for (auto v : f.s) { lo = std::min<int>(lo, v); hi = std::max<int>(hi, v); }
    return hi - lo;
}

int sumOf(const Frame& f)
{
    int t = 0;
    for (auto v : f.s) t += int(v);
    return t;
}

/// A ramp with a hard edge: known input for the shapers that have to move it.
Frame rampFrame()
{
    Frame f;
    for (int i = 0; i < 32; ++i) f.s[size_t(i)] = uint8_t(i / 2);
    return f;
}

} // namespace

TEST_CASE("the sources are the bank's own shapes", "[synth]")
{
    // A synth sine, triangle, saw and square are the frames the bank's
    // generators write, byte for byte -- one wave, one definition.
    CHECK(render(plain(SynthSource::Sine)).s == frameSine().s);
    CHECK(render(plain(SynthSource::Triangle)).s == frameTriangle().s);
    CHECK(render(plain(SynthSource::Saw)).s == frameSaw().s);

    Synth sq = plain(SynthSource::Square);
    sq.start.width = 8;
    sq.end = sq.start;
    CHECK(render(sq).s == framePulse(8).s);

    // The pulse width is a number that moves: eight samples high, then
    // twenty-four.
    sq.start.width = 24;
    sq.end = sq.start;
    CHECK(render(sq).s == framePulse(24).s);
}

TEST_CASE("the additive source is eight partials, and one of them is a sine", "[synth]")
{
    Synth s = plain(SynthSource::Additive);
    s.start.partials = { { 15, 0, 0, 0, 0, 0, 0, 0 } };
    s.end = s.start;
    CHECK(render(s).s == frameSine().s);

    // The second partial alone is the same shape twice as fast: it comes
    // back to the same level every sixteen samples.
    s.start.partials = { { 0, 15, 0, 0, 0, 0, 0, 0 } };
    s.end = s.start;
    const Frame two = render(s);
    for (int i = 0; i < 16; ++i) CHECK(two.s[size_t(i)] == two.s[size_t(i + 16)]);

    // Every partial at once still fits: the sum is scaled to the rails, so
    // nothing clips before a shaper sees it.
    s.start.partials.fill(15);
    s.end = s.start;
    CHECK(peakToPeak(render(s)) == 15);
}

TEST_CASE("the noise source is the seed's, and the drawn source is the frame's", "[synth]")
{
    Synth a = plain(SynthSource::Noise);
    a.seed = 7;
    Synth b = a;
    b.seed = 8;
    CHECK(render(a).s == render(a).s);
    CHECK(render(a).s != render(b).s);

    Frame drawn;
    for (int i = 0; i < 32; ++i) drawn.s[size_t(i)] = uint8_t((i * 5) % 16);
    CHECK(render(plain(SynthSource::Drawn), drawn).s == drawn.s);
}

TEST_CASE("an amount of zero is a no-op on every shaper", "[synth]")
{
    const Frame bare = render(plain(SynthSource::Saw));
    for (int k = 1; k < kSynthShaperCount; ++k) {
        const auto shaper = SynthShaper(k);
        INFO(synthShaperName(shaper));
        CHECK(render(withShaper(SynthSource::Saw, shaper, 0)).s == bare.s);
    }
}

TEST_CASE("the filters take the harmonics they say they do", "[synth]")
{
    // A saw is every harmonic. A deep low-pass leaves the fundamental, which
    // crosses the middle exactly twice in a cycle -- a level is an integer,
    // so 2s - 15 is never zero and the sign is always one thing or the other.
    const Frame lp = render(withShaper(SynthSource::Saw, SynthShaper::LowPass, 15));
    int crossings = 0;
    for (int i = 0; i < 32; ++i) {
        const int a = 2 * int(lp.s[size_t(i)]) - 15, b = 2 * int(lp.s[size_t((i + 1) % 32)]) - 15;
        if ((a > 0) != (b > 0)) ++crossings;
    }
    CHECK(crossings == 2);

    // A high-pass takes the low harmonics away, so what is left swings less
    // than the saw did.
    const Frame hp = render(withShaper(SynthSource::Saw, SynthShaper::HighPass, 8));
    CHECK(peakToPeak(hp) < peakToPeak(render(plain(SynthSource::Saw))));

    // Resonance is a peak at the corner, so the same band-pass with more of
    // it is a different wave.
    const Frame flat = render(withShaper(SynthSource::Saw, SynthShaper::BandPass, 4, 0));
    const Frame peaky = render(withShaper(SynthSource::Saw, SynthShaper::BandPass, 4, 12));
    CHECK(flat.s != peaky.s);

    // An all-pass moves the shape without moving the spectrum: a sine has
    // one harmonic, so it comes back a sine -- the same swing, somewhere else.
    const Frame ap = render(withShaper(SynthSource::Sine, SynthShaper::AllPass, 6));
    CHECK(ap.s != frameSine().s);
    CHECK(peakToPeak(ap) >= 14);
}

TEST_CASE("drive, rotation and the vertical shift do what they say", "[synth]")
{
    // Clip drives a sine towards a square: all but the four samples nearest
    // the zero crossings end up at a rail.
    const Frame clipped = render(withShaper(SynthSource::Sine, SynthShaper::Clip, 15));
    int railed = 0;
    for (auto v : clipped.s) if (v == 0 || v == 15) ++railed;
    CHECK(railed == 28);

    // Fold reflects instead of cutting, so the top of the sine comes back
    // down: the wave is no longer at its peak halfway up.
    const Frame folded = render(withShaper(SynthSource::Sine, SynthShaper::Fold, 15));
    CHECK(folded.s != clipped.s);
    CHECK(peakToPeak(folded) > 0);

    // Wrap jumps to the other rail instead: a driven sine crosses it.
    const Frame wrapped = render(withShaper(SynthSource::Sine, SynthShaper::Wrap, 15));
    CHECK(wrapped.s != clipped.s);

    // Rotate turns the cycle two samples a step, and the sign is the way round.
    const Frame saw = render(plain(SynthSource::Saw));
    const Frame right = render(withShaper(SynthSource::Saw, SynthShaper::Rotate, 3));
    for (int i = 0; i < 32; ++i) CHECK(right.s[size_t(i)] == saw.s[size_t((i + 32 - 6) % 32)]);
    const Frame left = render(withShaper(SynthSource::Saw, SynthShaper::Rotate, -3));
    for (int i = 0; i < 32; ++i) CHECK(left.s[size_t(i)] == saw.s[size_t((i + 6) % 32)]);

    // Shift moves the whole wave, up on a positive amount and down on a
    // negative one; the rails hold it.
    const Frame up = render(withShaper(SynthSource::Sine, SynthShaper::Shift, 5));
    const Frame down = render(withShaper(SynthSource::Sine, SynthShaper::Shift, -5));
    CHECK(sumOf(up) > sumOf(frameSine()));
    CHECK(sumOf(down) < sumOf(frameSine()));
}

TEST_CASE("invert, reverse, smooth, crush, quantise and normalise", "[synth]")
{
    const Frame ramp = rampFrame();
    Synth drawn = plain(SynthSource::Drawn);

    // Invert at 15 turns the wave upside down about the middle.
    Synth inv = withShaper(SynthSource::Drawn, SynthShaper::Invert, 15);
    const Frame flipped = render(inv, ramp);
    for (int i = 0; i < 32; ++i) CHECK(int(flipped.s[size_t(i)]) == 15 - int(ramp.s[size_t(i)]));

    // Reverse at 15 plays the cycle backwards, sample 0 staying where it is.
    const Frame back = render(withShaper(SynthSource::Drawn, SynthShaper::Reverse, 15), ramp);
    for (int i = 0; i < 32; ++i) CHECK(back.s[size_t(i)] == ramp.s[size_t((32 - i) % 32)]);

    // Smooth rounds the ramp's one hard edge off, so the biggest step between
    // two samples shrinks.
    auto biggestStep = [](const Frame& f) {
        int m = 0;
        for (int i = 0; i < 32; ++i) m = std::max(m, std::abs(int(f.s[size_t(i)]) - int(f.s[size_t((i + 1) % 32)])));
        return m;
    };
    CHECK(biggestStep(render(withShaper(SynthSource::Drawn, SynthShaper::Smooth, 6), ramp)) < biggestStep(ramp));

    // Bit-crush at 14 leaves two levels: the rails.
    const Frame crushed = render(withShaper(SynthSource::Drawn, SynthShaper::Crush, 14), ramp);
    for (auto v : crushed.s) CHECK((v == 0 || v == 15));

    // Quantise holds the wave in steps: at an amount of 3, four samples at a
    // time.
    const Frame held = render(withShaper(SynthSource::Drawn, SynthShaper::Quantise, 3), ramp);
    for (int i = 0; i < 32; ++i) CHECK(held.s[size_t(i)] == held.s[size_t((i / 4) * 4)]);

    // Normalise takes a small wave out to the rails.
    Frame small;
    for (int i = 0; i < 32; ++i) small.s[size_t(i)] = uint8_t(i < 16 ? 7 : 8);
    CHECK(peakToPeak(render(withShaper(SynthSource::Drawn, SynthShaper::Normalise, 15), small)) > peakToPeak(small));
    (void) drawn;
}

TEST_CASE("the shapers run in the order the chain lists them", "[synth]")
{
    Synth a = plain(SynthSource::Saw);
    a.chain[0] = SynthShaper::Rotate;
    a.chain[1] = SynthShaper::Smooth;
    a.start.amount[0] = 4;
    a.start.amount[1] = 5;
    a.end = a.start;

    Synth b = a;
    b.chain[0] = SynthShaper::Smooth;
    b.chain[1] = SynthShaper::Rotate;
    // Smoothing an edge then turning it is not turning it then smoothing the
    // edge that has moved past the wrap.
    CHECK(render(a).s != render(b).s);
}

TEST_CASE("the morph's ends are the start and the end states", "[synth]")
{
    Synth s = plain(SynthSource::Square);
    s.frames = 8;
    s.start.width = 4;
    s.end.width = 28;

    std::vector<Frame> run;
    synthesize(s, Frame{}, run);
    REQUIRE(run.size() == 8);

    Synth atStart = s;
    atStart.frames = 1;
    atStart.end = atStart.start;
    CHECK(run.front().s == synthesizeFrame(atStart, Frame{}, 0).s);
    CHECK(run.front().s == framePulse(4).s);

    Synth atEnd = s;
    atEnd.frames = 1;
    atEnd.start = atEnd.end;
    CHECK(run.back().s == synthesizeFrame(atEnd, Frame{}, 0).s);
    CHECK(run.back().s == framePulse(28).s);

    // In between the width really moves: the runs of high samples grow.
    auto high = [](const Frame& f) { int n = 0; for (auto v : f.s) if (v >= 8) ++n; return n; };
    for (size_t i = 1; i < run.size(); ++i) CHECK(high(run[i]) >= high(run[i - 1]));

    // A one-frame run is the start state, whatever the end says.
    Synth one = s;
    one.frames = 1;
    std::vector<Frame> single;
    synthesize(one, Frame{}, single);
    REQUIRE(single.size() == 1);
    CHECK(single.front().s == framePulse(4).s);
}

TEST_CASE("a morph moves the shapers as well as the source", "[synth]")
{
    Synth s = plain(SynthSource::Saw);
    s.frames = 5;
    s.chain[0] = SynthShaper::LowPass;
    s.start.amount[0] = 0;             // wide open
    s.end.amount[0] = 15;              // closed to the fundamental
    std::vector<Frame> run;
    synthesize(s, Frame{}, run);
    REQUIRE(run.size() == 5);
    CHECK(run.front().s == frameSaw().s);
    // The saw's hard edge softens as the run goes on.
    auto biggestStep = [](const Frame& f) {
        int m = 0;
        for (int i = 0; i < 32; ++i) m = std::max(m, std::abs(int(f.s[size_t(i)]) - int(f.s[size_t((i + 1) % 32)])));
        return m;
    };
    CHECK(biggestStep(run.back()) < biggestStep(run.front()));
}

TEST_CASE("the synth is deterministic", "[synth]")
{
    // Every source and every shaper, twice: the exporter ships frames, so a
    // run has to be the same bytes whenever it is asked for.
    for (int src = 0; src < kSynthSourceCount; ++src)
        for (int k = 0; k < kSynthShaperCount; ++k) {
            Synth s = plain(SynthSource(src));
            s.frames = 4;
            s.seed = uint8_t(3 + src);
            s.chain[0] = SynthShaper(k);
            s.chain[1] = SynthShaper((k + 5) % kSynthShaperCount);
            s.start.amount[0] = int8_t(k - 7);
            s.start.amount[1] = 9;
            s.start.resonance[0] = uint8_t(k);
            s.end.amount[0] = int8_t(15 - k);
            s.end.amount[1] = -4;
            s.end.width = 20;
            std::vector<Frame> a, b;
            synthesize(s, rampFrame(), a);
            synthesize(s, rampFrame(), b);
            REQUIRE(a.size() == b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                INFO(synthSourceName(SynthSource(src)) << " / " << synthShaperName(SynthShaper(k)) << " frame " << i);
                CHECK(a[i].s == b[i].s);
            }
        }
}

TEST_CASE("a frame is always four bits", "[synth]")
{
    for (int src = 0; src < kSynthSourceCount; ++src)
        for (int k = 0; k < kSynthShaperCount; ++k)
            for (int amount = -15; amount <= 15; amount += 5) {
                const Synth s = withShaper(SynthSource(src), SynthShaper(k), amount, 15);
                const Frame f = render(s, rampFrame());
                for (auto v : f.s) CHECK(v <= 15);
            }
}

TEST_CASE("each end of the morph has its own shape", "[synth]")
{
    // Section 36: a run can go from a sine to a saw. The ends are the two
    // shapes exactly and the middle is the crossfade.
    Synth s = plain(SynthSource::Sine);
    s.end.source = SynthSource::Saw;
    s.frames = 5;
    std::vector<Frame> run;
    synthesize(s, Frame{}, run);
    REQUIRE(run.size() == 5);
    CHECK(run.front().s == frameSine().s);
    CHECK(run.back().s == frameSaw().s);
    CHECK(run[2].s != run.front().s);
    CHECK(run[2].s != run.back().s);
    // Sample 8 is the sine's peak (15) and the saw's 4: the middle frame is between.
    CHECK(run[2].s[8] < 15);
    CHECK(run[2].s[8] > 4);

    // The same shape at both ends is the one shape, byte for byte, as before.
    Synth same = plain(SynthSource::Triangle);
    same.frames = 3;
    std::vector<Frame> flat;
    synthesize(same, Frame{}, flat);
    for (const auto& f : flat) CHECK(f.s == frameTriangle().s);
}

TEST_CASE("the run is written from its first frame and the rest of the wave stays", "[synth]")
{
    // Section 36: From and To place the run inside the slot.
    Synth s = plain(SynthSource::Saw);
    s.first = 4;
    s.frames = 3;
    CHECK(synthFirstFrame(s) == 4);
    CHECK(synthFrameCount(s) == 3);
    std::vector<Frame> run;
    synthesize(s, Frame{}, run);
    REQUIRE(run.size() == 3);

    // A two-frame wave grows to seven: the gap copies the run's last frame.
    Wave w;
    w.frames = { rampFrame(), frameSine() };
    synthWriteRun(s, run, w);
    REQUIRE(w.frames.size() == 7);
    CHECK(w.frames[0].s == rampFrame().s);
    CHECK(w.frames[1].s == frameSine().s);
    CHECK(w.frames[2].s == frameSaw().s);
    CHECK(w.frames[3].s == frameSaw().s);
    for (int k = 4; k < 7; ++k) CHECK(w.frames[size_t(k)].s == run[size_t(k - 4)].s);

    // A longer wave keeps what lies past the run.
    Wave big;
    for (int k = 0; k < 10; ++k) big.frames.push_back(k % 2 ? frameSine() : rampFrame());
    synthWriteRun(s, run, big);
    REQUIRE(big.frames.size() == 10);
    for (int k = 0; k < 4; ++k) CHECK(big.frames[size_t(k)].s == (k % 2 ? frameSine() : rampFrame()).s);
    for (int k = 7; k < 10; ++k) CHECK(big.frames[size_t(k)].s == (k % 2 ? frameSine() : rampFrame()).s);

    // The count is clamped to what fits after the first frame.
    Synth tail = plain(SynthSource::Sine);
    tail.first = 14;
    tail.frames = 16;
    CHECK(synthFrameCount(tail) == 2);
    std::vector<Frame> two;
    synthesize(tail, Frame{}, two);
    Wave sixteen;
    synthWriteRun(tail, two, sixteen);
    CHECK(sixteen.frames.size() == 16);
}
