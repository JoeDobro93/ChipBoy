// chipboy_fuzz -- random songs through the whole plugin, to find the crash
// nobody would have written by hand (docs/COMMANDS_AND_TEMPO.md section 28).
//
// One seed builds one song: random phrases 1-64 steps long with their own
// grooves, random cells on all four channels -- notes, instruments, tables,
// velocities and every command letter with random arguments -- chained for 64
// rows, with random note sources. It is written through the song writer and
// opened through the reader, so the file format is fuzzed with the engine.
//
// The song then plays on the plugin's own transport in blocks of random size,
// with tempo changes, PLAYS changes, locates and stops thrown in, and at the
// end every channel is silenced. A run fails when:
//
//   * a sample is NaN or infinite,
//   * a block takes longer than the time bound (a hang, or an accidental
//     quadratic somewhere),
//   * anything is still audible 100 ms after all notes off.
//
// Every run prints its seed, so a failure can be replayed with --seed.
//
//   chipboy_fuzz [--seeds N] [--seed S] [--rows N] [--out DIR] [--quiet]
//
// Exit code 0 when every seed passed.
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/BankJson.h"
#include "plugin/shared/SongFiles.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace chipboy;
using namespace chipboy::plugin;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kMaxBlock = 1024;
constexpr double kBlockBudgetMs = 400.0;   ///< a block of at most 1024 frames; a hang is orders out
constexpr double kSilence = 1.0e-3;

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

void setParameter(ChipBoyProcessor& p, const juce::String& id, double value)
{
    if (auto* prm = p.apvts.getParameter(id))
        prm->setValueNotifyingHost(prm->getNormalisableRange().convertTo0to1(float(value)));
}

/// A small deterministic generator, so a seed means the same song on every
/// machine and in every build (std::mt19937 would too, but this is one line
/// and the distributions below are the ones that matter).
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return uint32_t(s >> 33); }
    int      range(int lo, int hi) { return hi <= lo ? lo : lo + int(next() % uint32_t(hi - lo + 1)); }
    bool     chance(int percent) { return int(next() % 100u) < percent; }
};

/// Every letter, so the fuzz covers the command table rather than the letters
/// somebody remembered. H only means anything inside a table, and it is here
/// on purpose: a cell that holds one must be harmless.
constexpr bank::Cmd kLetters[] = {
    bank::Cmd::A, bank::Cmd::C, bank::Cmd::D, bank::Cmd::E, bank::Cmd::F, bank::Cmd::G,
    bank::Cmd::H, bank::Cmd::K, bank::Cmd::L, bank::Cmd::M, bank::Cmd::O, bank::Cmd::P,
    bank::Cmd::R, bank::Cmd::S, bank::Cmd::T, bank::Cmd::V, bank::Cmd::W, bank::Cmd::Z
};

bank::Command randomCommand(Rng& r)
{
    bank::Command c;
    c.cmd = kLetters[r.next() % (sizeof(kLetters) / sizeof(kLetters[0]))];
    // Arguments right across the byte, not only the sane range: a song file
    // can hold anything and the driver has to clamp it.
    c.a = int16_t(r.range(0, 255));
    c.b = int16_t(r.range(0, 255));
    if (r.chance(10)) c = bank::revertOf(c.cmd);      // the revert form, where the letter has one
    if (c.cmd == bank::Cmd::T) c.a = int16_t(r.range(40, 255));   // a tempo the clock can hold
    return c;
}

/// A random song: phrases of their own lengths and grooves, cells on every
/// channel, and a chain of `rows` rows each.
std::unique_ptr<tracker::Song> randomSong(Rng& r, int rows)
{
    auto owned = std::make_unique<tracker::Song>();
    tracker::Song& s = *owned;
    s.tempoBpm = double(r.range(40, 255));
    for (int g = 0; g < 16; ++g) {
        auto& gr = s.grooves[size_t(g)];
        gr.ticks = {};
        const int n = r.range(1, 6);
        for (int k = 0; k < n; ++k) gr.ticks[size_t(k)] = uint8_t(r.range(1, 24));
    }
    const int phrases = r.range(1, 24);
    for (int i = 0; i < phrases; ++i) {
        auto& p = s.phrases[size_t(i)];
        p.used = true;
        p.steps = uint8_t(r.range(1, tracker::kMaxSteps));
        p.groove = uint8_t(r.chance(40) ? r.range(0, 16) : 0);
        for (int k = 0; k < p.length(); ++k) {
            auto& c = p.cells[size_t(k)];
            if (r.chance(45)) c.note = uint8_t(r.chance(10) ? tracker::kNoteOff : r.range(1, 127));
            if (r.chance(30)) c.inst = uint8_t(r.range(1, 12));
            if (r.chance(15)) c.table = uint8_t(r.range(1, 8));
            if (r.chance(20)) c.vel = uint8_t(r.range(1, 127));
            if (r.chance(35)) c.cmd1 = randomCommand(r);
            if (r.chance(20)) c.cmd2 = randomCommand(r);
        }
    }
    for (int ch = 0; ch < 4; ++ch) {
        s.chain[size_t(ch)].clear();
        for (int row = 0; row < rows; ++row)
            s.chain[size_t(ch)].push_back(uint8_t(r.chance(12) ? 0 : r.range(1, phrases)));
        s.noteSource[size_t(ch)] = tracker::NoteSource(r.range(1, 2));   // Trkr or Hybrid
    }
    tracker::buildRowTables(s);
    return owned;
}

struct Result {
    bool ok = true;
    juce::String why;
    double worstBlockMs = 0.0;
    double tail = 0.0;        ///< the widest swing inside a tail block: what "silent" means
    double tailDc = 0.0;      ///< and the offset it sits at, which a RAW model holds
    int64_t samples = 0;
    int model = 0;
};

/// One seed: build the song, write it, open it, play it about, silence it.
Result runSeed(uint64_t seed, int rows, const juce::File& dir)
{
    Rng r(seed);
    Result out;
    const auto song = randomSong(r, rows);
    const auto bank = std::make_unique<bank::Bank>(bank::Bank::factory());
    const juce::File file = dir.getChildFile("fuzz_" + juce::String(int(seed)) + ".cbsong");
    if (!saveSong(*song, *bank, file, "Factory")) { out.ok = false; out.why = "the song could not be written"; return out; }

    const auto pOwned = std::make_unique<ChipBoyProcessor>();
    auto& p = *pOwned;
    SongReport report;
    if (!p.openSongFileInTab(file, report)) { out.ok = false; out.why = "the song file could not be read back"; return out; }
    p.closeTab(0);
    setParameter(p, ids::noise, 0.0);           // the hiss and the display line would never be silent
    setParameter(p, ids::lcd, 0.0);
    out.model = r.range(0, 2);
    setParameter(p, ids::model, double(out.model));
    setParameter(p, ids::notesOnTick, r.chance(50) ? 1.0 : 0.0);
    p.prepareToPlay(kSampleRate, kMaxBlock);
    p.setLoop(r.chance(30));
    p.transportPlay();                          // no play head: the plugin's own transport (16)

    const auto live = p.song();
    const int64_t total = live ? tracker::songTicks(*live) : 0;
    const double tempo = live ? std::clamp(live->tempoBpm, 40.0, 255.0) : 120.0;
    const int64_t samples = int64_t(double(total) * 60.0 * kSampleRate / (tempo * double(driver::kTicksPerBeat)));
    out.samples = samples;

    juce::AudioBuffer<float> buffer(2, kMaxBlock);
    juce::MidiBuffer midi;
    int64_t played = 0;
    int blocks = 0;
    while (played < samples && blocks < 20000) {
        const int n = r.range(32, kMaxBlock);
        buffer.setSize(2, n, false, false, true);
        midi.clear();
        // A few MIDI notes: a Hybrid channel takes its notes from them, and a
        // Trkr channel has to ignore them.
        if (r.chance(25)) {
            const int ch = r.range(0, 3);
            midi.addEvent(juce::MidiMessage::noteOn(ch + 1, r.range(24, 96), juce::uint8(r.range(1, 127))), r.range(0, n - 1));
        }
        if (r.chance(20)) midi.addEvent(juce::MidiMessage::noteOff(r.range(1, 4), r.range(24, 96)), r.range(0, n - 1));
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock(buffer, midi);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        out.worstBlockMs = std::max(out.worstBlockMs, ms);
        if (ms > kBlockBudgetMs) {
            out.ok = false;
            out.why = "a block of " + juce::String(n) + " frames took " + juce::String(ms, 1) + " ms";
            return out;
        }
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                if (!std::isfinite(buffer.getSample(c, i))) {
                    out.ok = false;
                    out.why = "block " + juce::String(blocks) + " sample " + juce::String(i) + " is not finite";
                    return out;
                }
        played += n;
        ++blocks;
        // The things a player does while it plays.
        if (r.chance(4)) setParameter(p, ids::songTempo, double(r.range(40, 255)));
        if (r.chance(3)) setParameter(p, channelParamId(r.range(0, 3), ids::source), double(r.range(0, 2)));
        if (r.chance(2)) { p.transportStop(); p.transportPlay(); }        // a locate back to the start
        if (r.chance(2)) p.setLoopRows(r.range(0, rows - 1), r.chance(50) ? -1 : r.range(1, rows));
        if ((blocks % 32) == 0) pump(1);
    }

    // Every channel silent, and it stays silent: the transport stops and each
    // channel takes an all-notes-off, then 100 ms of tail is let through.
    p.transportStop();
    // A host's panic: every MIDI channel, since a ChipBoy channel listens to
    // the one its Source parameter names and a random song sets those about.
    midi.clear();
    for (int ch = 1; ch <= 16; ++ch) {
        midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(ch, 120, 0), 0);
    }
    buffer.setSize(2, 512, false, false, true);
    p.processBlock(buffer, midi);
    midi.clear();
    for (int b = 0; b < int(0.100 * kSampleRate / 512.0) + 1; ++b) { buffer.setSize(2, 512, false, false, true); p.processBlock(buffer, midi); }
    // ... and now nothing may come out of it. Silence is a level that does
    // not move: a DAC switched off holds its last level on this hardware
    // (reference section 9), and the RAW model has no coupling to take that
    // offset away, so what is measured is the swing inside a block -- a note
    // still sounding fills it, a DC offset drifting to nothing does not.
    for (int b = 0; b < 8; ++b) {
        buffer.setSize(2, 512, false, false, true);
        p.processBlock(buffer, midi);
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            const int n = buffer.getNumSamples();
            double lo = 1.0e30, hi = -1.0e30;
            for (int i = 0; i < n; ++i) {
                const double v = double(buffer.getSample(c, i));
                if (!std::isfinite(v)) { out.ok = false; out.why = "the tail is not finite"; return out; }
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            // What the offset drifted by over the block is not sound; what the
            // signal did on top of that is. A level walking one way covers its
            // whole range with the drift and leaves nothing here.
            const double drift = std::fabs(double(buffer.getSample(c, n - 1)) - double(buffer.getSample(c, 0)));
            out.tail = std::max(out.tail, (hi - lo) - drift);
            out.tailDc = std::max(out.tailDc, std::max(std::fabs(lo), std::fabs(hi)));
        }
    }
    if (out.tail > kSilence) {
        out.ok = false;
        out.why = "still audible 100 ms after all notes off: " + juce::String(out.tail, 6) + " of swing in a block";
        for (int ch = 0; ch < 4; ++ch) {
            const auto& v = p.driverView().view(ch);
            std::printf("     ch%d active %d dac %d vol %d level %d instrument %d\n", ch + 1, int(v.active), int(v.dacOn),
                        int(v.volume), p.channelLevels[size_t(ch)].load(), int(v.instrument));
        }
    }
    pump(5);
    file.deleteFile();
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    int seeds = 8, rows = 64;
    uint64_t first = 1;
    bool quiet = false, seedsGiven = false;
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        auto nextArg = [&]() { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        if (a == "--seeds") { seeds = std::max(1, nextArg().getIntValue()); seedsGiven = true; }
        else if (a == "--seed") { first = uint64_t(nextArg().getLargeIntValue()); if (!seedsGiven) seeds = 1; }
        else if (a == "--rows") rows = std::clamp(nextArg().getIntValue(), 1, 512);
        else if (a == "--out") { dir = juce::File(nextArg()); dir.createDirectory(); }
        else if (a == "--quiet") quiet = true;
        else if (a == "--help" || a == "-h") {
            std::printf("chipboy_fuzz [--seeds N] [--seed S] [--rows N] [--out DIR] [--quiet]\n");
            return 0;
        }
    }
    int failed = 0;
    double worst = 0.0;
    for (int k = 0; k < seeds; ++k) {
        const uint64_t seed = first + uint64_t(k);
        const Result r = runSeed(seed, rows, dir);
        worst = std::max(worst, r.worstBlockMs);
        if (!r.ok) {
            std::printf("FAIL seed %llu (model %s): %s\n", (unsigned long long)seed,
                        r.model == 0 ? "DMG" : r.model == 1 ? "CGB" : "RAW", r.why.toRawUTF8());
            std::printf("     replay it with: chipboy_fuzz --seed %llu --rows %d\n", (unsigned long long)seed, rows);
            ++failed;
        } else if (!quiet) {
            std::printf("ok   seed %llu: %d rows, %.2f s, model %s, worst block %.1f ms, tail %.6f swing (%.6f dc)\n",
                        (unsigned long long)seed, rows, double(r.samples) / kSampleRate,
                        r.model == 0 ? "DMG" : r.model == 1 ? "CGB" : "RAW", r.worstBlockMs, r.tail, r.tailDc);
        }
    }
    if (failed) { std::printf("\nFAILED %d of %d seeds\n", failed, seeds); return 1; }
    std::printf("\nPASSED %d seeds, %d rows each, worst block %.1f ms\n", seeds, rows, worst);
    return 0;
}
