#include "core/Bank/Bank.h"

#include <algorithm>
#include <cmath>

namespace chipboy::bank {

namespace {
constexpr double kPi = 3.14159265358979323846;
uint8_t q4(double x) { return uint8_t(std::clamp(int(std::lround(7.5 + 7.5 * x)), 0, 15)); }
}

const char* cmdLetter(Cmd c)
{
    static const char* L[] = { "", "A", "C", "D", "E", "F", "G", "H", "K", "L", "M", "O", "P", "R", "S", "T", "V", "W", "Z" };
    return L[int(c) <= kCmdCount ? int(c) : 0];
}
Cmd cmdFromLetter(char c)
{
    for (int i = 1; i <= kCmdCount; ++i) if (cmdLetter(Cmd(i))[0] == c) return Cmd(i);
    return Cmd::None;
}

Instrument Instrument::defaults(InstrumentType t, const char* name)
{
    Instrument i;
    i.used = true;
    i.name = name;
    i.type = t;
    if (t == InstrumentType::Kit) i.noteOff = NoteOff::Ignore;
    return i;
}

const Instrument* Bank::instrument(int slot) const { return slot >= 1 && slot <= kInstrumentSlots && instruments[size_t(slot - 1)].used ? &instruments[size_t(slot - 1)] : nullptr; }
const Table*      Bank::table(int slot) const      { return slot >= 1 && slot <= kTableSlots && tables[size_t(slot - 1)].used ? &tables[size_t(slot - 1)] : nullptr; }
const Wave*       Bank::wave(int slot) const       { return slot >= 1 && slot <= kWaveSlots && waves[size_t(slot - 1)].used ? &waves[size_t(slot - 1)] : nullptr; }
const Kit*        Bank::kit(int slot) const        { return slot >= 1 && slot <= kKitSlots && kits[size_t(slot - 1)].used ? &kits[size_t(slot - 1)] : nullptr; }

Frame frameSine()     { Frame f; for (int i = 0; i < 32; ++i) f.s[size_t(i)] = q4(std::sin(2.0 * kPi * (i + 0.5) / 32.0)); return f; }
Frame frameTriangle() { Frame f; for (int i = 0; i < 32; ++i) f.s[size_t(i)] = uint8_t(i < 16 ? i : 31 - i); return f; }
Frame frameSaw()      { Frame f; for (int i = 0; i < 32; ++i) f.s[size_t(i)] = uint8_t(i / 2); return f; }
Frame framePulse(int w){ Frame f; for (int i = 0; i < 32; ++i) f.s[size_t(i)] = uint8_t(i < w ? 15 : 0); return f; }
Frame frameInterpolate(const Frame& a, const Frame& b, double t)
{
    Frame f;
    for (int i = 0; i < 32; ++i) f.s[size_t(i)] = uint8_t(std::clamp(int(std::lround(a.s[size_t(i)] * (1.0 - t) + b.s[size_t(i)] * t)), 0, 15));
    return f;
}

uint16_t periodForSampleRate(double hz)
{
    const double p = 2048.0 - 2097152.0 / std::max(1024.0, hz);
    return uint16_t(std::clamp(int(std::lround(p)), 0, 2047));
}
double sampleRateForPeriod(uint16_t period) { return 2097152.0 / double(2048 - std::min<int>(period, 2047)); }

Bank Bank::empty() { return Bank{}; }

namespace {

// Deterministic noise for the factory kit.
struct Lcg { uint32_t s = 12345; double next() { s = s * 1103515245u + 12345u; return ((s >> 8) & 0xFFFF) / 32768.0 - 1.0; } };

KitSample synthSample(const char* name, uint8_t note, double seconds, double rate, int kind)
{
    KitSample k;
    k.name = name;
    k.note = note;
    const int n = int(seconds * rate);
    k.data.resize(size_t(n));
    Lcg rng;
    double hp = 0.0, lp = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / rate;
        double x = 0.0;
        switch (kind) {
            case 0: {   // kick: pitch sweep with a click
                const double f = 45.0 + 160.0 * std::exp(-t * 28.0);
                static double ph = 0.0; if (i == 0) ph = 0.0; ph += 2.0 * kPi * f / rate;
                x = std::sin(ph) * std::exp(-t * 9.0) + (t < 0.004 ? 0.6 : 0.0);
                break; }
            case 1: {   // snare: tone burst plus noise
                const double tone = std::sin(2.0 * kPi * 185.0 * t) * std::exp(-t * 30.0);
                x = 0.5 * tone + 0.8 * rng.next() * std::exp(-t * 16.0);
                break; }
            case 2: {   // closed hat: high-passed noise, short
                const double v = rng.next(); const double y = v - hp; hp = v; x = 1.4 * y * std::exp(-t * 60.0);
                break; }
            case 3: {   // open hat: longer
                const double v = rng.next(); const double y = v - hp; hp = v; x = 1.4 * y * std::exp(-t * 11.0);
                break; }
            case 4: {   // clap: three bursts
                const double env = (t < 0.012 ? 1 : t < 0.024 ? 0.8 : t < 0.036 ? 0.65 : std::exp(-(t - 0.036) * 25.0));
                const double v = rng.next(); lp = 0.6 * lp + 0.4 * v; x = 1.2 * lp * env;
                break; }
            default: {  // tom
                const double f = 90.0 + 60.0 * std::exp(-t * 12.0);
                static double ph2 = 0.0; if (i == 0) ph2 = 0.0; ph2 += 2.0 * kPi * f / rate;
                x = std::sin(ph2) * std::exp(-t * 7.0);
                break; }
        }
        k.data[size_t(i)] = q4(std::clamp(x, -1.0, 1.0));
    }
    return k;
}

Table makeTable(const char* name, std::initializer_list<int8_t> transposes, TableEnd end = TableEnd::Loop)
{
    Table t; t.used = true; t.name = name; t.end = end;
    int i = 0;
    for (int8_t v : transposes) { if (i >= kTableSteps) break; t.steps[size_t(i)].hasTranspose = true; t.steps[size_t(i)].transpose = v; ++i; }
    return t;
}

} // namespace

Bank Bank::factory()
{
    Bank b;
    auto& I = b.instruments;
    auto pulse = [&](int slot, const char* name, uint8_t duty, uint8_t vol, EnvDir dir, uint8_t rate) {
        auto& i = I[size_t(slot - 1)]; i = Instrument::defaults(InstrumentType::Pulse, name);
        i.duty = duty; i.envVol = vol; i.envDir = dir; i.envRate = rate; return &i; };
    auto* lead = pulse(1, "Square lead", 2, 13, EnvDir::Down, 0);   lead->vib = { VibShape::Triangle, 3, 3, 10 };
    auto* pluck = pulse(2, "Pluck", 1, 15, EnvDir::Down, 2);        (void)pluck;
    auto* bass = pulse(3, "Bass 25", 1, 14, EnvDir::Down, 0);       bass->length = 0;
    auto* sweep = pulse(4, "Sweep down", 2, 15, EnvDir::Down, 3);   sweep->sweepRate = 3; sweep->sweepDown = true; sweep->sweepShift = 2;
    auto* arp = pulse(5, "Chord arp", 0, 12, EnvDir::Down, 1);      arp->table = 1;
    auto* dutyseq = pulse(6, "Duty cycler", 2, 13, EnvDir::Down, 0); dutyseq->dutySeqLen = 8; dutyseq->dutySeq = { 2, 2, 1, 1, 0, 0, 1, 1 };

    auto wave = [&](int slot, const char* name, uint8_t w, uint8_t adv, FrameLoop loop) {
        auto& i = I[size_t(slot - 1)]; i = Instrument::defaults(InstrumentType::Wave, name);
        i.wave = w; i.frameAdvance = adv; i.frameLoop = loop; i.waveLevel = 3; return &i; };
    wave(7, "Triangle bass", 1, 0, FrameLoop::Loop);
    wave(8, "Saw", 2, 0, FrameLoop::Loop);
    wave(9, "Organ frames", 5, 3, FrameLoop::PingPong);
    wave(10, "Tri to saw", 6, 4, FrameLoop::Once);

    auto noise = [&](int slot, const char* name, uint8_t vol, uint8_t rate, bool lfsr7, uint8_t shift, uint8_t div) {
        auto& i = I[size_t(slot - 1)]; i = Instrument::defaults(InstrumentType::Noise, name);
        i.envVol = vol; i.envDir = EnvDir::Down; i.envRate = rate; i.lfsr7 = lfsr7; i.noiseManual = true; i.noiseShift = shift; i.noiseDivisor = div; return &i; };
    auto* kick = noise(11, "Kick", 15, 1, false, 6, 3);   kick->table = 4; kick->noiseSweep = -1;
    noise(12, "Snare", 13, 2, false, 4, 4);
    noise(13, "Hat closed", 9, 1, true, 1, 4);
    auto* hatOpen = noise(14, "Hat open", 9, 4, true, 1, 4); (void)hatOpen;
    noise(15, "Crash", 12, 6, false, 2, 1);

    auto& kitInst = I[15]; kitInst = Instrument::defaults(InstrumentType::Kit, "Kit 1"); kitInst.kit = 1;

    // Tables
    b.tables[0] = makeTable("Arp minor", { 0, 3, 7, 12, 0, 3, 7, 12, 0, 3, 7, 12, 0, 3, 7, 12 });
    b.tables[1] = makeTable("Arp major", { 0, 4, 7, 12, 0, 4, 7, 12, 0, 4, 7, 12, 0, 4, 7, 12 });
    { Table t; t.used = true; t.name = "Vol fade"; for (int i = 0; i < 16; ++i) t.steps[size_t(i)].vol = int8_t(15 - i); t.end = TableEnd::Stop; b.tables[2] = t; }
    // The kick's pitch drop is the instrument's own noise sweep now that S is
    // PU1 only; the table shapes its level instead.
    { Table t; t.used = true; t.name = "Kick shape"; const int8_t v[] = { 15, 12, 8, 4 }; for (int i = 0; i < 4; ++i) t.steps[size_t(i)].vol = v[i]; t.end = TableEnd::Stop; b.tables[3] = t; }
    { Table t; t.used = true; t.name = "Slide up"; t.steps[0].cmd1 = { Cmd::L, 6, 0, 0 }; t.steps[0].hasTranspose = true; t.steps[0].transpose = -12; t.end = TableEnd::Stop; b.tables[4] = t; }
    { Table t; t.used = true; t.name = "Octave hop"; t.steps[0].hasTranspose = true; t.steps[0].transpose = 12; t.steps[1].hasTranspose = true; t.steps[1].transpose = 0; t.end = TableEnd::Loop; b.tables[5] = t; }

    // Waves
    auto W = [&](int slot, const char* name, std::vector<Frame> frames) { auto& w = b.waves[size_t(slot - 1)]; w.used = true; w.name = name; w.frames = std::move(frames); };
    W(1, "Triangle", { frameTriangle() });
    W(2, "Saw", { frameSaw() });
    W(3, "Sine", { frameSine() });
    W(4, "Pulse 25", { framePulse(8) });
    { std::vector<Frame> fr; Frame a = frameSine(), c = frameSaw(); for (int k = 0; k < 4; ++k) fr.push_back(frameInterpolate(a, c, k / 3.0)); W(5, "Organ", fr); }
    { std::vector<Frame> fr; Frame a = frameTriangle(), c = frameSaw(); for (int k = 0; k < 6; ++k) fr.push_back(frameInterpolate(a, c, k / 5.0)); W(6, "Tri to saw", fr); }

    // Kit: synthesized drums at 11 468 Hz, the conventional rate.
    Kit k; k.used = true; k.name = "909-ish"; k.period = periodForSampleRate(11468.0);
    const double rate = sampleRateForPeriod(k.period);
    k.samples.push_back(synthSample("KICK", 36, 0.30, rate, 0));
    k.samples.push_back(synthSample("SNAR", 38, 0.22, rate, 1));
    k.samples.push_back(synthSample("CHAT", 42, 0.07, rate, 2));
    k.samples.push_back(synthSample("OHAT", 46, 0.30, rate, 3));
    k.samples.push_back(synthSample("CLAP", 39, 0.18, rate, 4));
    k.samples.push_back(synthSample("TOM", 45, 0.28, rate, 5));
    b.kits[0] = std::move(k);
    return b;
}

} // namespace chipboy::bank
