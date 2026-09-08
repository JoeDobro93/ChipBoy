// chipboy_recordtest -- the demo is recorded, then played back, and the two
// runs must drive the chip identically (docs/COMMANDS_AND_TEMPO.md 9.5).
//
// Pass 1 plays Demo/chipboy_demo.mid into the processor under a fake play head
// at 120 BPM, 4/4, 48 kHz, 512-sample blocks, with every channel's note source
// set to Trk and record armed, while Demo/chipboy_demo_automation.json drives
// the parameters exactly as the Reaper projects do. The recorded song is saved
// through the JSON writer.
//
// Pass 2 loads that song into a fresh processor, holds every automated lane at
// its bar-1 value, sends no MIDI, and plays the same length.
//
// The two passes are compared per channel: the same register writes, in the
// same order, with the same values, within 64 samples of each other. NR50 and
// NR51 are compared as a fifth stream, because M and O are command letters.
// A difference prints the bar, the step, the channel and both writes.
//
//   chipboy_recordtest [--demo DIR] [--out DIR]
//
// Exit code 0 when the two passes agree.
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/BankJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace chipboy;
using namespace chipboy::plugin;

namespace {

constexpr double  kSampleRate = 48000.0;
constexpr int     kBlock = 512;
constexpr int     kBars = 16;            // the demo, ...
constexpr int     kTailBars = 1;         // ... and one bar of tail
constexpr int64_t kTimeTolerance = 64;   // samples (section 9.5)
constexpr size_t  kContext = 8;          // writes printed either side of a difference

/* ------------------------------------------------------------ the host */

struct FakePlayHead : juce::AudioPlayHead {
    int64_t frame = 0;
    double  bpm = 120.0;
    bool    playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setBpm(bpm);
        p.setTimeInSamples(frame);
        p.setTimeInSeconds(double(frame) / kSampleRate);
        p.setPpqPosition(double(frame) / kSampleRate * bpm / 60.0);
        p.setTimeSignature(TimeSignature { 4, 4 });
        return p;
    }
};

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

/* ------------------------------------------------------ the automation */

/// Demo/chipboy_demo_automation.json: the same tables the Reaper envelopes and
/// the MIDI file come from. Values are in plugin units, beats are quarter
/// notes from the start of bar 1, and every point sits on a step.
struct Lane {
    juce::String id;
    std::vector<std::pair<double, double>> points;   // (beat, value)

    /// The value in force at a beat: the last point at or before it.
    double at(double beat) const
    {
        double v = points.empty() ? 0.0 : points.front().second;
        for (const auto& p : points) { if (p.first > beat + 1e-9) break; v = p.second; }
        return v;
    }
};

struct Automation {
    double bpm = 120.0;
    std::vector<std::pair<juce::String, double>> statics;
    std::vector<Lane> lanes;
};

bool readLanes(const juce::var& obj, std::vector<Lane>& out)
{
    auto* o = obj.getDynamicObject();
    if (o == nullptr) return false;
    for (const auto& prop : o->getProperties()) {
        Lane lane;
        lane.id = prop.name.toString();
        const auto* points = prop.value.getArray();
        if (points == nullptr) return false;
        for (const auto& point : *points) {
            const auto* pair = point.getArray();
            if (pair == nullptr || pair->size() != 2) return false;
            lane.points.emplace_back(double((*pair)[0]), double((*pair)[1]));
        }
        out.push_back(std::move(lane));
    }
    return true;
}

bool loadAutomation(const juce::File& file, Automation& out)
{
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    auto* o = parsed.getDynamicObject();
    if (o == nullptr || o->getProperty("format").toString() != "chipboy-demo-automation") return false;
    out.bpm = double(o->getProperty("bpm"));
    if (auto* statics = o->getProperty("static").getDynamicObject())
        for (const auto& prop : statics->getProperties())
            out.statics.emplace_back(prop.name.toString(), double(prop.value));
    return readLanes(o->getProperty("lanes"), out.lanes);
}

void setParameter(ChipBoyProcessor& p, const juce::String& id, double value)
{
    // The host-automation path: the parameter's own range, notified from the
    // message thread between blocks, exactly as a host moves a lane.
    auto* prm = p.apvts.getParameter(id);
    if (prm != nullptr) prm->setValueNotifyingHost(prm->getNormalisableRange().convertTo0to1(float(value)));
}

/* -------------------------------------------------------------- the MIDI */

struct TimedMessage { int64_t sample = 0; juce::MidiMessage message; };

bool loadMidi(const juce::File& file, double bpm, std::vector<TimedMessage>& out)
{
    juce::FileInputStream in(file);
    if (!in.openedOk()) return false;
    juce::MidiFile midi;
    if (!midi.readFrom(in)) return false;
    const int ppq = midi.getTimeFormat();
    if (ppq <= 0) return false;                       // SMPTE time: the demo is not
    // A tick is 60 / bpm / ppq seconds. At 120 BPM, 960 PPQ and 48 kHz that is
    // exactly 25 samples, so every sixteenth lands on a whole sample.
    const double samplesPerTick = 60.0 / bpm / double(ppq) * kSampleRate;
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* ev : *midi.getTrack(t)) {
            const auto& m = ev->message;
            if (!m.isNoteOnOrOff() && !m.isController() && !m.isPitchWheel()) continue;
            out.push_back({ int64_t(std::llround(m.getTimeStamp() * samplesPerTick)), m });
        }
    // Stable: at one sample the tracks keep their order and each track keeps
    // its own, which is the order the generator wrote (a note-off before the
    // note-on that follows it, keyswitches before the notes they select for).
    std::stable_sort(out.begin(), out.end(), [](const TimedMessage& a, const TimedMessage& b) { return a.sample < b.sample; });
    return true;
}

/* ------------------------------------------------------- register writes */

/// Which comparison stream a register belongs to: one per channel, plus the
/// master pair NR50/NR51 (M and O are letters, so they are compared too).
constexpr int kStreams = 5;
const char* kStreamName[kStreams] = { "PU1", "PU2", "WAV", "NOI", "master" };

int streamOf(uint16_t addr)
{
    if (addr >= 0xFF10 && addr <= 0xFF14) return 0;
    if (addr >= 0xFF15 && addr <= 0xFF19) return 1;
    if (addr >= 0xFF1A && addr <= 0xFF1E) return 2;
    if (addr >= 0xFF30 && addr <= 0xFF3F) return 2;   // wave RAM belongs to WAV
    if (addr >= 0xFF1F && addr <= 0xFF23) return 3;
    if (addr >= 0xFF24 && addr <= 0xFF26) return 4;
    return -1;
}

struct Write { int64_t sample = 0; uint64_t cycle = 0; uint16_t addr = 0; uint8_t value = 0; };

struct Capture {
    std::array<std::vector<Write>, kStreams> streams;
    std::vector<double> rmsPerBar;
};

const char* regName(uint16_t addr)
{
    static const char* names[] = { "NR10", "NR11", "NR12", "NR13", "NR14", "----", "NR21", "NR22", "NR23", "NR24",
                                   "NR30", "NR31", "NR32", "NR33", "NR34", "----", "NR41", "NR42", "NR43", "NR44",
                                   "NR50", "NR51", "NR52" };
    if (addr >= 0xFF10 && addr <= 0xFF26) return names[addr - 0xFF10];
    if (addr >= 0xFF30 && addr <= 0xFF3F) return "WAVE";
    return "??";
}

/* -------------------------------------------------------------- the run */

struct RunOptions {
    bool record = false;
    const std::vector<TimedMessage>* midi = nullptr;   // null: no MIDI at all
    bool holdAtBarOne = false;                         // lanes frozen at their bar-1 value
};

double beatOfSample(int64_t sample, double bpm) { return double(sample) / kSampleRate * bpm / 60.0; }

void run(ChipBoyProcessor& p, const Automation& aut, const RunOptions& opt, Capture& cap, std::vector<driver::RegWrite>& log)
{
    const double samplesPerBeat = 60.0 / aut.bpm * kSampleRate;
    const int blocks = int((int64_t(std::llround(double(kBars + kTailBars) * 4.0 * samplesPerBeat)) + kBlock - 1) / kBlock);

    p.prepareToPlay(kSampleRate, kBlock);
    FakePlayHead head;
    head.bpm = aut.bpm;
    p.setPlayHead(&head);
    p.setWriteLog(&log);
    for (const auto& s : aut.statics) setParameter(p, s.first, s.second);
    if (opt.record) p.setRecordArm(true);

    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    size_t next = 0;
    const size_t bars = size_t(kBars + kTailBars);
    std::vector<double> barSum(bars, 0.0), barN(bars, 0.0);

    for (int b = 0; b < blocks; ++b) {
        const int64_t f0 = int64_t(b) * kBlock;
        head.frame = f0;
        // A lane's value takes effect in the block that holds its point, so it
        // is in force at the tick it names -- that block's first tick, since
        // ticks are 1000 samples apart at 120 BPM and blocks are 512.
        const double blockEndBeat = beatOfSample(f0 + kBlock - 1, aut.bpm);
        for (const auto& lane : aut.lanes)
            setParameter(p, lane.id, lane.at(opt.holdAtBarOne ? 0.0 : blockEndBeat));

        midi.clear();
        if (opt.midi != nullptr)
            while (next < opt.midi->size() && (*opt.midi)[next].sample < f0 + kBlock) {
                midi.addEvent((*opt.midi)[next].message, int((*opt.midi)[next].sample - f0));
                ++next;
            }
        p.processBlock(buffer, midi);

        const size_t bar = size_t(std::min<int64_t>(int64_t(beatOfSample(f0, aut.bpm) / 4.0), int64_t(bars) - 1));
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int i = 0; i < kBlock; ++i) { const double v = buffer.getSample(c, i); barSum[bar] += v * v; barN[bar] += 1.0; }

        if (opt.record || b % 16 == 0) pump(1);
    }
    p.setRecordArm(false);
    pump(400);                       // the recorder's FIFO is applied on a timer
    p.setWriteLog(nullptr);
    p.setPlayHead(nullptr);

    cap.rmsPerBar.assign(bars, 0.0);
    for (size_t i = 0; i < bars; ++i) cap.rmsPerBar[i] = barN[i] > 0.0 ? std::sqrt(barSum[i] / barN[i]) : 0.0;
    for (const auto& w : log) {
        if (w.addr == driver::Driver::kAlignToQuietEdge) continue;
        const int s = streamOf(w.addr);
        if (s < 0) continue;
        cap.streams[size_t(s)].push_back({ int64_t(std::llround(double(w.cycle) * kSampleRate / double(kCpuHz))), w.cycle, w.addr, w.value });
    }
}

/* ------------------------------------------------------------- listings */

juce::String cmdText(const bank::Command& c)
{
    if (c.cmd == bank::Cmd::None) return "-";
    const auto* info = commandInfo(c.cmd);
    juce::String text = juce::String(bank::cmdLetter(c.cmd)) + " " + juce::String(int(c.a));
    if (info == nullptr || info->nargs > 1) text += " " + juce::String(int(c.b));
    return text;
}

juce::String noteText(uint8_t note)
{
    if (note == 0) return "-";
    if (note == tracker::kNoteOff) return "OFF";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[note % 12]) + juce::String(int(note) / 12 - 1) + " (" + juce::String(int(note)) + ")";
}

void writeCellListing(const juce::File& file, const tracker::Song& song)
{
    juce::StringArray lines;
    lines.add("# the cells chipboy_recordtest recorded from Demo/chipboy_demo.mid");
    lines.add("# " + juce::String(song.steps()) + " steps per bar, straight groove");
    lines.add("");
    lines.add("bar step ch   note        vel inst table  cmd1        cmd2");
    for (int ch = 0; ch < 4; ++ch) {
        const auto& chain = song.chain[size_t(ch)];
        for (size_t barIndex = 0; barIndex < chain.size(); ++barIndex) {
            const auto* phrase = song.phrase(chain[barIndex]);
            if (phrase == nullptr) continue;
            for (int step = 0; step < song.steps(); ++step) {
                const auto& c = phrase->steps[size_t(step)];
                if (c.note == 0 && c.inst == 0 && c.table == 0 && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None) continue;
                const bool sounds = c.note != 0 && c.note != tracker::kNoteOff;
                lines.add(juce::String(int(barIndex) + 1).paddedLeft(' ', 3)
                          + juce::String(step).paddedLeft(' ', 5) + "  "
                          + juce::String(kStreamName[ch]) + "  "
                          + noteText(c.note).paddedRight(' ', 10)
                          + (sounds ? juce::String(int(tracker::velocityOf(c))) : juce::String("-")).paddedLeft(' ', 5)
                          + (c.inst ? juce::String(int(c.inst)) : juce::String("-")).paddedLeft(' ', 5)
                          + (c.table ? juce::String(int(c.table)) : juce::String("-")).paddedLeft(' ', 6) + "  "
                          + cmdText(c.cmd1).paddedRight(' ', 12)
                          + cmdText(c.cmd2));
            }
        }
        lines.add("");
    }
    file.replaceWithText(lines.joinIntoString("\n") + "\n");
}

/* ---------------------------------------------------------- the compare */

int barOf(int64_t sample, double bpm) { return int(beatOfSample(sample, bpm) / 4.0) + 1; }
int stepOf(int64_t sample, double bpm)
{
    const double beat = beatOfSample(sample, bpm);
    return int(std::floor((beat - std::floor(beat / 4.0) * 4.0) * 4.0));
}

juce::String writeText(const Write& w, double bpm)
{
    return juce::String::formatted("%s (%04X) = %02X at sample %lld (bar %d step %d)",
                                   regName(w.addr), int(w.addr), int(w.value), (long long) w.sample,
                                   barOf(w.sample, bpm), stepOf(w.sample, bpm));
}

/// Every captured write, one per line: what to diff when a run goes wrong.
void dumpWrites(const juce::File& file, const Capture& cap, double bpm)
{
    juce::StringArray lines;
    for (int s = 0; s < kStreams; ++s) {
        for (size_t i = 0; i < cap.streams[size_t(s)].size(); ++i) {
            const auto& w = cap.streams[size_t(s)][i];
            lines.add(juce::String(kStreamName[s]) + " " + juce::String(int(i)).paddedLeft(' ', 6)
                      + "  " + writeText(w, bpm) + " cycle " + juce::String(juce::int64(w.cycle)));
        }
    }
    file.replaceWithText(lines.joinIntoString("\n") + "\n");
}

bool compare(const Capture& a, const Capture& b, double bpm)
{
    for (int s = 0; s < kStreams; ++s) {
        const auto& x = a.streams[size_t(s)];
        const auto& y = b.streams[size_t(s)];
        const size_t n = std::min(x.size(), y.size());
        for (size_t i = 0; i < n; ++i) {
            if (x[i].addr == y[i].addr && x[i].value == y[i].value
                && std::llabs((long long) (x[i].sample - y[i].sample)) <= kTimeTolerance) continue;
            std::printf("FAIL %s: write %d differs, bar %d step %d\n", kStreamName[s], int(i),
                        barOf(x[i].sample, bpm), stepOf(x[i].sample, bpm));
            std::printf("     record: %s\n", writeText(x[i], bpm).toRawUTF8());
            std::printf("     replay: %s\n", writeText(y[i], bpm).toRawUTF8());
            // The writes around it, so the shape of the difference is visible.
            const size_t from = i > kContext ? i - kContext : 0;
            for (size_t k = from; k < std::min(n, i + kContext + 1); ++k)
                std::printf("     %s %3d  record %s | replay %s\n", k == i ? "->" : "  ", int(k),
                            writeText(x[k], bpm).toRawUTF8(), writeText(y[k], bpm).toRawUTF8());
            return false;
        }
        if (x.size() != y.size()) {
            const auto& extra = x.size() > y.size() ? x[n] : y[n];
            std::printf("FAIL %s: the record pass wrote %d, the replay %d; the first write only one pass has:\n",
                        kStreamName[s], int(x.size()), int(y.size()));
            std::printf("     %s pass: %s\n", x.size() > y.size() ? "record" : "replay", writeText(extra, bpm).toRawUTF8());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::File demoDir = juce::File(CHIPBOY_DEMO_DIR);
    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile("recordtest");
    bool dump = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String key(argv[i]);
        if (key == "--dump") dump = true;                     // every write, to diff by hand
        else if (i + 1 >= argc) continue;
        else if (key == "--demo") demoDir = juce::File(juce::String(argv[++i]));
        else if (key == "--out") outDir = juce::File(juce::String(argv[++i]));
    }
    outDir.createDirectory();

    Automation aut;
    const auto autFile = demoDir.getChildFile("chipboy_demo_automation.json");
    if (!loadAutomation(autFile, aut)) {
        std::printf("FAIL cannot read %s\n", autFile.getFullPathName().toRawUTF8());
        return 1;
    }
    std::vector<TimedMessage> midi;
    if (!loadMidi(demoDir.getChildFile("chipboy_demo.mid"), aut.bpm, midi)) {
        std::printf("FAIL cannot read %s\n", demoDir.getChildFile("chipboy_demo.mid").getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf("demo: %d MIDI events, %d static parameters, %d automation lanes\n",
                int(midi.size()), int(aut.statics.size()), int(aut.lanes.size()));

    /* ---- pass 1: play the demo in, record it ------------------------- */
    juce::String recorded;
    Capture recordPass;
    {
        ChipBoyProcessor p;
        p.mutateSong([](tracker::Song& s) { for (auto& n : s.noteSource) n = tracker::NoteSource::Tracker; });
        std::vector<driver::RegWrite> log;
        log.reserve(1u << 20);
        RunOptions opt;
        opt.record = true;
        opt.midi = &midi;
        run(p, aut, opt, recordPass, log);

        const auto song = p.song();
        if (song == nullptr) { std::printf("FAIL the record pass produced no song\n"); return 1; }
        recorded = songToJson(*song);
        outDir.getChildFile("recorded_song.json").replaceWithText(recorded);
        writeCellListing(outDir.getChildFile("recorded_cells.txt"), *song);
        int cells = 0;
        for (int ch = 0; ch < 4; ++ch)
            for (auto slot : song->chain[size_t(ch)])
                if (const auto* phrase = song->phrase(slot))
                    for (const auto& c : phrase->steps)
                        if (c.note || c.inst || c.table || c.cmd1.cmd != bank::Cmd::None || c.cmd2.cmd != bank::Cmd::None) ++cells;
        std::printf("recorded %d cells into %s\n", cells, outDir.getChildFile("recorded_cells.txt").getFullPathName().toRawUTF8());
        if (dump) dumpWrites(outDir.getChildFile("writes_record.txt"), recordPass, aut.bpm);
    }

    /* ---- pass 2: play the recorded song back ------------------------- */
    Capture replayPass;
    {
        ChipBoyProcessor p;
        auto song = std::make_shared<tracker::Song>();
        if (!songFromJson(recorded, *song)) { std::printf("FAIL the recorded song does not read back\n"); return 1; }
        for (auto& n : song->noteSource) n = tracker::NoteSource::Tracker;
        p.publishSong(std::move(song));
        std::vector<driver::RegWrite> log;
        log.reserve(1u << 20);
        RunOptions opt;
        opt.holdAtBarOne = true;
        run(p, aut, opt, replayPass, log);
        if (dump) dumpWrites(outDir.getChildFile("writes_replay.txt"), replayPass, aut.bpm);
    }

    /* ---- compare ----------------------------------------------------- */
    const bool ok = compare(recordPass, replayPass, aut.bpm);
    std::printf("\nregister writes per channel\n");
    for (int s = 0; s < kStreams; ++s)
        std::printf("  %-7s record %6d   replay %6d\n", kStreamName[s],
                    int(recordPass.streams[size_t(s)].size()), int(replayPass.streams[size_t(s)].size()));
    std::printf("output RMS per bar\n");
    for (size_t bar = 0; bar < recordPass.rmsPerBar.size(); ++bar)
        std::printf("  bar %2d   record %.5f   replay %.5f\n", int(bar) + 1,
                    recordPass.rmsPerBar[bar], bar < replayPass.rmsPerBar.size() ? replayPass.rmsPerBar[bar] : 0.0);
    std::printf("\n%s\n", ok ? "PASSED the record test" : "FAILED the record test");
    return ok ? 0 : 1;
}
