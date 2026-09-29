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
// Pass 3 does the same from Demo/ChipBoy Demo.cbsong -- the song file written
// from pass 1 -- with no play head at all, so the plugin's own transport runs
// it at the Song tempo, as the Standalone does (section 16).
//
// Pass 4 is Hybrid (section 20): the demo song in a tab, all four channels
// Hybrid, the MIDI file played and no automation at all beyond the two static
// parameters the project needs -- the notes come from MIDI and everything
// else from the song's cells, and it must drive the chip exactly as pass 1
// did. The same configuration is what --write-state writes.
//
// The passes are compared per channel: the same register writes, in the
// same order, with the same values, within 64 samples of each other. NR50 and
// NR51 are compared as a fifth stream, because M and O are command letters.
// A difference prints the bar, the step, the channel and both writes.
//
// --play-song is a different job in the same tool (section 24): it opens a
// song file in a tab and plays it on the plugin's own transport, listening to
// what comes out rather than comparing two passes. It is what CTest's
// demo_songs_load runs over Demo/songs.
//
//   chipboy_recordtest [--demo DIR] [--out DIR] [--dump]
//   chipboy_recordtest --write-song FILE     write the recorded song and stop
//   chipboy_recordtest --check-song FILE     record and compare against FILE
//   chipboy_recordtest --write-state FILE    write the hybrid project's state
//   chipboy_recordtest --check-state FILE    build it and compare against FILE
//   chipboy_recordtest --play-song FILE [bars]   play a song file and hear it
//   chipboy_recordtest --play-midi SONG.cbsong FILE.mid [bars]   a MIDI file through a song's map (section 225)
//   chipboy_recordtest --remake-midi SONG.cbsong OUTDIR          the song's cells as a MIDI file and a map
//   chipboy_recordtest --check-remake SONG.cbsong FILE.mid [bars] the cells against the MIDI, write for write
//   chipboy_recordtest --record-midi SONG.cbsong FILE.mid OUT.cbsong [bars] [--expect FILE]
//                                        record a MIDI file into the song (section 226); the
//                                        expect file lists what every row must have become
//   chipboy_recordtest --trace-song FILE OUT.csv [seconds] [--tempo BPM] [--from TICK]
//   chipboy_recordtest --export-midi FILE OUT.mid [--no-noise]     (section 224)
//                                        the register writes a song file makes, in
//                                        the lsdjref trace's CSV, to set beside an
//                                        LSDj trace; --tempo plays it at another
//                                        tempo, as a host's would
//
// Exit code 0 when the passes agree.
#include "core/Apu/Apu.h"
#include "core/Console.h"
#include "core/Driver/Clock.h"
#include "core/Export/MidiExport.h"
#include "core/Driver/Driver.h"
#include "core/Import/LsdjSong.h"
#include "core/Tracker/Player.h"
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/BankJson.h"
#include "plugin/shared/LsdjImport.h"
#include "plugin/shared/SongFiles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace chipboy;
using namespace chipboy::plugin;

namespace {

int    gConsole = -1;           // --console dmg|cgb|raw: the Model parameter for --play-song, else the song's own state
double gSampleRate = 48000.0;   // --rate: the render's sample rate (the demo checks stay at 48 kHz)
int    gBlock = 512;            // --block: its block size
#define kSampleRate gSampleRate
#define kBlock gBlock
constexpr int     kBars = 16;            // the demo, ...
constexpr int     kTailBars = 1;         // ... and one bar of tail
constexpr int64_t kTimeTolerance = 140;  // samples (section 9.5): one 358 Hz instant -- the wave's sync wait can
                                          // hold an instant's work for up to 2.8 ms, and which instant catches the
                                          // boundary turns on the DIV phase of the note-on (sections 171, 178)
constexpr size_t  kContext = 8;          // writes printed either side of a difference

/// The instance UUID and name the written state carries. A fresh processor
/// makes a random UUID; pinning it is what makes the file the same bytes
/// every run (section 20).
constexpr const char* kStateUuid = "3E9C1B44-5D2A-4F17-9C63-C1B0A7E5D820";
constexpr const char* kStateName = "ChipBoy 1";

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
    bool ownTransport = false;                         // no play head: the plugin runs itself
    bool noLanes = false;                              // no automation lane is written at all
    bool skipInertLanes = false;                       // Hybrid: no Instrument / Table / CMD / keyswitch statics
};

/// The static parameters a Hybrid channel does not read (section 20): its
/// Instrument, Table and command slots and its keyswitch octave are inert, so
/// the hybrid pass leaves them alone and the song's cells do the work.
bool inertUnderHybrid(const juce::String& id)
{
    return id.contains("keyswitch") || id.contains("instrument") || id.contains("table")
           || id.contains("cmd1") || id.contains("cmd2");
}

double beatOfSample(int64_t sample, double bpm) { return double(sample) / kSampleRate * bpm / 60.0; }

void run(ChipBoyProcessor& p, const Automation& aut, const RunOptions& opt, Capture& cap, std::vector<driver::RegWrite>& log)
{
    const double samplesPerBeat = 60.0 / aut.bpm * kSampleRate;
    // Whole blocks inside the bars, none past them: the tick that starts the
    // bar after the tail is the song's end, where a channel plays its chain
    // round again (section 212), and that pass is not the demo's.
    const int blocks = int(int64_t(std::llround(double(kBars + kTailBars) * 4.0 * samplesPerBeat)) / kBlock);

    p.prepareToPlay(kSampleRate, kBlock);
    FakePlayHead head;
    head.bpm = aut.bpm;
    if (!opt.ownTransport) p.setPlayHead(&head);
    p.setWriteLog(&log);
    for (const auto& s : aut.statics)
        if (!(opt.skipInertLanes && inertUnderHybrid(s.first))) setParameter(p, s.first, s.second);
    // Section 160: the tracker's ticks sit on the ROM's grid, so the live
    // notes of a pass that plays MIDI -- the record pass, the hybrid pass --
    // wait for the tick too (the Quantise MIDI notes to ticks toggle), and
    // land where the replay pass replays them as cells.
    if (opt.midi != nullptr) setParameter(p, ids::notesOnTick, 1.0);
    if (opt.record) p.setRecordArm(true);
    if (opt.ownTransport) {
        // The Standalone's case (section 16): no host transport, so the
        // plugin's own clock plays the song at the Song tempo, once through.
        p.setLoop(false);
        p.transportPlay();
    }

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
        if (!opt.noLanes)
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
    if (opt.ownTransport) p.transportStop();
    pump(400);                       // the recorder's FIFO is applied on a timer
    p.setWriteLog(nullptr);
    p.setPlayHead(nullptr);

    cap.rmsPerBar.assign(bars, 0.0);
    for (size_t i = 0; i < bars; ++i) cap.rmsPerBar[i] = barN[i] > 0.0 ? std::sqrt(barSum[i] / barN[i]) : 0.0;
    for (const auto& w : log) {
        const int s = streamOf(w.addr);
        if (s < 0) continue;
        cap.streams[size_t(s)].push_back({ int64_t(std::llround(double(w.cycle) * kSampleRate / double(kCpuHz))), w.cycle, w.addr, w.value });
    }
}

/* ------------------------------------------------------------- listings */

juce::String cmdText(const bank::Command& c)
{
    if (c.cmd == bank::Cmd::None) return "-";
    // The revert form carries no value: it puts the letter back (section 9.4).
    if (bank::isRevert(c)) return juce::String(bank::cmdLetter(c.cmd)) + " =";
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
    lines.add("# phrases of their own length, straight groove (section 25)");
    lines.add("");
    lines.add("row step ch   note        vel inst table  cmd1        cmd2");
    for (int ch = 0; ch < 4; ++ch) {
        const auto& chain = song.chain[size_t(ch)];
        for (size_t barIndex = 0; barIndex < chain.size(); ++barIndex) {
            const auto* phrase = song.phrase(chain[barIndex]);
            if (phrase == nullptr) continue;
            for (int step = 0; step < phrase->length(); ++step) {
                const auto& c = phrase->cells[size_t(step)];
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
            // The writes around it, so the shape of the difference is visible
            // (CHIPBOY_CONTEXT widens the window when a shape needs it).
            size_t context = kContext;
            if (const char* c = std::getenv("CHIPBOY_CONTEXT"); c != nullptr) context = size_t(std::max(1, std::atoi(c)));
            const size_t from = i > context ? i - context : 0;
            for (size_t k = from; k < std::min(n, i + context + 1); ++k)
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

/// The hybrid project (section 20): the demo song in a tab with the bank the
/// file carries, all four channels Hybrid, and the two static parameters the
/// project needs -- the MIDI channel PU1 listens on and NOI's velocity mode.
/// Everything else the demo used is inert under Hybrid or a default.
///
/// The tab is built from the file's contents rather than opened from it, so
/// the state this writes holds no absolute path and is the same bytes on any
/// machine.
bool buildHybridProject(ChipBoyProcessor& p, const juce::File& songFile, const Automation& aut, bool statics)
{
    const auto song = std::make_unique<tracker::Song>();      // 300 KB: never on the stack
    const auto bank = std::make_unique<bank::Bank>();
    SongReport report;
    if (!loadSong(songFile, *song, report, nullptr, bank.get())) return false;
    if (!report.hasBank) { std::printf("FAIL %s carries no bank; it is not a format-5 song file\n", songFile.getFullPathName().toRawUTF8()); return false; }
    p.addTab(std::shared_ptr<const tracker::Song>(new tracker::Song(*song)),
             std::shared_ptr<const bank::Bank>(new bank::Bank(*bank)),
             songFile.getFileNameWithoutExtension(), report.bankName);
    p.closeTab(0);                                            // the empty tab a fresh plugin starts with
    p.mutateSong([](tracker::Song& s) { for (auto& n : s.noteSource) n = tracker::NoteSource::Hybrid; });
    if (statics)
        for (const auto& s : aut.statics)
            if (!inertUnderHybrid(s.first)) setParameter(p, s.first, s.second);
    return true;
}

/* ------------------------------------------------ --trace-song */

/// `chipboy_recordtest --trace-song FILE OUT.csv [seconds]`: the song file's
/// bank and song go through the core alone -- clock, player, driver, as the
/// parity harness's compare tool drives a case -- and every register write is
/// written as `cycle,addr,name,value`, the lsdjref trace's columns, so an
/// imported song can be set beside the LSDj trace of the save it came from.
int traceSong(const juce::File& file, const juce::File& out, double seconds, double bpm, int64_t fromTick)
{
    auto bank = std::make_unique<bank::Bank>();
    auto song = std::make_unique<tracker::Song>();
    SongReport report;
    if (!plugin::loadSong(file, *song, report, nullptr, bank.get())) { std::printf("FAIL cannot open %s as a song file\n", file.getFullPathName().toRawUTF8()); return 1; }

    driver::Clock clock;
    tracker::Player player;
    driver::Driver driver;
    clock.prepare(kSampleRate);
    player.prepare(kSampleRate);
    driver.prepare(kSampleRate, bank.get(), song.get(), chipboy::Console::DMG);
    player.setSong(song.get());
    driver::ClockConfig cc;
    cc.source = driver::TempoSource::Song;
    // `--tempo` plays the same song at another tempo, which is what a host's
    // tempo does to it: the pitch must not move with it (section 147).
    cc.songTempo = std::clamp(bpm > 0.0 ? bpm : song->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
    cc.lsdjTempo = song->lsdjTempo;
    clock.setConfig(cc);
    if (!song->tempoMap.empty()) clock.setTempoMap(song->tempoMap.data(), song->tempoMap.size());
    clock.setOwnsTransport(true);
    // `--from TICK` locates before playing (section 223): what a jump into
    // the middle of a song lands on, against the same stretch played through.
    if (fromTick > 0) clock.ownLocate(fromTick);
    clock.ownPlay();
    std::vector<driver::RegWrite> log;
    log.reserve(1u << 18);
    driver.setWriteLog(&log);
    for (int ch = 0; ch < 4; ++ch) driver.setParams(ch, driver::ChannelParams{});
    std::vector<driver::NoteEvent> events;
    std::vector<driver::RegWrite> blockWrites;
    const auto cycleAt = [](uint64_t f) { return uint64_t(double(f) * double(chipboy::kCpuHz) / kSampleRate); };
    const int64_t blocks = int64_t(seconds * kSampleRate / kBlock) + 1;
    uint64_t frame = 0;
    for (int64_t b = 0; b < blocks; ++b) {
        driver::Transport t;
        clock.process(t, uint32_t(kBlock), frame);
        for (int ch = 0; ch < 4; ++ch) {
            const int slot = driver.tableGrooveSlot(ch);
            driver.setTableGroove(ch, slot >= 1 && slot <= tracker::kGrooveSlots ? song->grooves[size_t(slot - 1)].ticks.data() : nullptr);
            driver.setViewGroove(ch, player.groove(ch));
        }
        events.clear();
        player.process(clock.ticks(), clock.tickCount(), clock.playing(), events);
        blockWrites.clear();
        // Section 141: the tick rate in force, for the shaped envelope's
        // sub-tick position on the very first tick.
        driver.setTickRate(clock.bpm() * double(chipboy::driver::kTicksPerBeat) / 60.0);
        driver.process(events.data(), events.size(), uint32_t(kBlock), frame, clock.ticks(), clock.tickCount(), cycleAt, blockWrites);
        frame += uint64_t(kBlock);
    }
    driver.setWriteLog(nullptr);
    std::stable_sort(log.begin(), log.end(), [](const driver::RegWrite& a, const driver::RegWrite& b) { return a.cycle < b.cycle; });
    juce::String text;
    text.preallocateBytes(log.size() * 32 + 64);
    text << "# chipboy_recordtest --trace-song " << file.getFileName() << "\ncycle,addr,name,value\n";
    const auto regName = [](uint16_t a) -> juce::String {
        static const char* names[] = { "NR10", "NR11", "NR12", "NR13", "NR14", "?", "NR21", "NR22", "NR23", "NR24", "NR30", "NR31", "NR32", "NR33", "NR34", "?", "NR41", "NR42", "NR43", "NR44", "NR50", "NR51", "NR52" };
        if (a >= 0xFF10 && a <= 0xFF26) return names[a - 0xFF10];
        return (a >= 0xFF30 && a <= 0xFF3F) ? "WAVE" : "?";
    };
    for (const auto& w : log)
        text << juce::String(juce::int64(w.cycle)) << "," << juce::String::toHexString(int(w.addr)).toUpperCase() << "," << regName(w.addr) << "," << juce::String::toHexString(int(w.value)).paddedLeft('0', 2).toUpperCase() << "\n";
    if (!out.replaceWithText(text)) { std::printf("FAIL cannot write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    std::printf("wrote %s: %d writes over %.1f s of %s\n", out.getFullPathName().toRawUTF8(), int(log.size()), seconds, file.getFileName().toRawUTF8());
    return 0;
}

/* ------------------------------------------------ --play-song (section 24) */

/// What one run heard: the RMS of every bar, the range the samples covered
/// (a channel that never moves is silence or a stuck DAC) and whether the
/// arithmetic went wrong anywhere.
struct SongRun {
    std::vector<double> rms;
    double peak = 0.0;
    double lo = 1.0e30, hi = -1.0e30;
    bool   bad = false;             ///< a NaN or an infinity came out
    int    badBlock = -1;
};

/// Where a song's rows sit, in ticks and in samples. Every channel keeps its
/// own time (section 25), so the listing counts in the rows of the longest
/// chain -- the song's own length -- taken from its prefix table.
struct SongShape {
    double  tempo = 120.0;
    int     steps = 16, songRows = 0, countChannel = 0;
    std::vector<int64_t> barStartSample;   ///< rows + 1 entries
    int64_t samples = 0;
};

/// A fresh processor with the file open in a tab of its own (section 18), the
/// analog noise floor off and one channel soloed (-1 for the whole mix). The
/// hiss and the display's line would make a dead channel look alive, and this
/// is a check on the chip, so both go off (section 21).
bool openForPlayback(ChipBoyProcessor& p, const juce::File& file, int solo, SongReport& report)
{
    if (!p.openSongFileInTab(file, report)) {
        std::printf("FAIL cannot open %s as a song file\n", file.getFullPathName().toRawUTF8());
        return false;
    }
    p.closeTab(0);                     // the empty tab a fresh plugin starts with
    setParameter(p, ids::noise, 0.0);
    setParameter(p, ids::lcd, 0.0);
    for (int ch = 0; ch < 4; ++ch) p.setChannelSolo(ch, solo == ch);
    return true;
}

juce::File gWavDir;   ///< --wav DIR: the mix and each soloed channel of --play-song as 48 kHz WAVs

bool playOnce(const juce::File& file, int solo, const SongShape& shape, SongRun& out)
{
    const auto pOwned = std::make_unique<ChipBoyProcessor>();
    auto& p = *pOwned;
    SongReport report;
    if (!openForPlayback(p, file, solo, report)) return false;

    p.prepareToPlay(kSampleRate, kBlock);
    if (gConsole >= 0) setParameter(p, ids::model, double(gConsole));   // the choice index; setParameter normalises
    p.setLoop(false);
    p.transportPlay();                 // no play head at all: the song's own clock (section 16)

    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    const size_t bars = shape.barStartSample.size() - 1;
    std::vector<double> sum, count;
    sum.assign(bars, 0.0);
    count.assign(bars, 0.0);
    out.rms.assign(bars, 0.0);
    const int blocks = int((shape.samples + kBlock - 1) / kBlock);
    size_t bar = 0;
    std::vector<float> wavL, wavR;
    if (gWavDir != juce::File()) { wavL.reserve(size_t(shape.samples)); wavR.reserve(size_t(shape.samples)); }
    for (int b = 0; b < blocks; ++b) {
        midi.clear();
        p.processBlock(buffer, midi);
        if (gWavDir != juce::File())
            for (int i = 0; i < kBlock; ++i) { wavL.push_back(buffer.getSample(0, i)); wavR.push_back(buffer.getSample(buffer.getNumChannels() > 1 ? 1 : 0, i)); }
        const int64_t f0 = int64_t(b) * kBlock;
        for (int i = 0; i < kBlock; ++i) {
            const int64_t f = f0 + i;
            if (f >= shape.samples) break;
            while (bar + 1 < bars && f >= shape.barStartSample[bar + 1]) ++bar;
            for (int c = 0; c < buffer.getNumChannels(); ++c) {
                const double v = double(buffer.getSample(c, i));
                if (!std::isfinite(v)) { if (!out.bad) out.badBlock = b; out.bad = true; continue; }
                sum[bar] += v * v;
                count[bar] += 1.0;
                out.lo = std::min(out.lo, v);
                out.hi = std::max(out.hi, v);
                out.peak = std::max(out.peak, std::fabs(v));
            }
        }
        if (b % 16 == 0) pump(1);
    }
    p.transportStop();
    pump(50);
    for (size_t i = 0; i < bars; ++i) out.rms[i] = count[i] > 0.0 ? std::sqrt(sum[i] / count[i]) : 0.0;
    if (!p.ownsTransport()) { std::printf("FAIL the plugin did not take the transport with no play head\n"); return false; }
    if (gWavDir != juce::File()) {
        gWavDir.createDirectory();
        const juce::File f = gWavDir.getChildFile(solo < 0 ? juce::String("mix.wav") : juce::String(kStreamName[solo]) + ".wav");
        f.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(f);
        std::unique_ptr<juce::AudioFormatWriter> w = fmt.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(kSampleRate).withNumChannels(2).withBitsPerSample(16));
        if (w != nullptr) {
            const float* chans[2] = { wavL.data(), wavR.data() };
            w->writeFromFloatArrays(chans, 2, int(wavL.size()));
            w.reset();
            std::printf("wrote %s (%d samples)\n", f.getFullPathName().toRawUTF8(), int(wavL.size()));
        }
    }
    return true;
}

/// `chipboy_recordtest --play-song FILE [bars]`: the file opens in a tab, the
/// plugin's own transport plays `bars` bars of it at the song's own tempo, and
/// what comes out is measured -- once for the mix and once per soloed channel,
/// since a channel's own audio is what says whether the arrangement plays.
/// It fails on a NaN, on silence over the whole run, and on a channel whose
/// audio never changes (it never sounded, or its DAC stood still).
int playSong(const juce::File& file, int bars)
{
    constexpr double kSilence = 1.0e-4;
    if (!file.existsAsFile()) { std::printf("FAIL %s does not exist\n", file.getFullPathName().toRawUTF8()); return 1; }

    SongShape shape;
    juce::String bankName;
    int phrases = 0, instruments = 0;
    juce::String sources;
    {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        auto& p = *pOwned;
        SongReport report;
        if (!openForPlayback(p, file, -1, report)) return 1;
        const auto song = p.song();
        if (song == nullptr) { std::printf("FAIL %s opened with no song\n", file.getFullPathName().toRawUTF8()); return 1; }
        shape.tempo = std::clamp(song->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
        shape.countChannel = tracker::longestChain(*song);
        shape.steps = song->stepsOfRow(shape.countChannel, 0);
        shape.songRows = song->rows();
        bankName = report.bankName;
        instruments = report.instrumentsUsed;
        for (int i = 0; i < tracker::kPhraseSlots; ++i) if (song->phrases[size_t(i)].used) ++phrases;
        for (int ch = 0; ch < 4; ++ch) {
            const auto s = song->noteSource[size_t(ch)];
            sources += juce::String(ch ? " " : "") + juce::String(kStreamName[ch]) + "="
                       + (s == tracker::NoteSource::Tracker ? "Trkr" : s == tracker::NoteSource::Hybrid ? "Hybrid" : "MIDI");
        }
        // Ticks to samples: the tick rate is tempo x 24 / 60 Hz (section 4),
        // and a row's ticks come from the song's own prefix table, so a phrase
        // of any length is as long as it really is (section 25).
        const double samplesPerTick = 60.0 * kSampleRate / (shape.tempo * double(driver::kTicksPerBeat));
        for (int b = 0; b <= bars; ++b)
            shape.barStartSample.push_back(int64_t(std::llround(double(tracker::rowStartTick(*song, shape.countChannel, b)) * samplesPerTick)));
        shape.samples = shape.barStartSample.back();
        if (shape.samples <= 0) { std::printf("FAIL %s is empty: no rows to play\n", file.getFullPathName().toRawUTF8()); return 1; }
    }
    std::printf("%s: %.0f BPM, %d steps in %s's first phrase, %d rows, %d phrases, %d instrument slots, bank \"%s\"\n",
                file.getFileNameWithoutExtension().toRawUTF8(), shape.tempo,
                shape.steps, kStreamName[shape.countChannel], shape.songRows, phrases, instruments, bankName.toRawUTF8());
    std::printf("plays %s; %d rows = %.2f s at %.0f BPM\n", sources.toRawUTF8(), bars,
                double(shape.samples) / kSampleRate, shape.tempo);

    SongRun mix;
    std::array<SongRun, 4> channels;
    if (!playOnce(file, -1, shape, mix)) return 1;
    for (int ch = 0; ch < 4; ++ch)
        if (!playOnce(file, ch, shape, channels[size_t(ch)])) return 1;

    std::printf("\nRMS per bar\n  bar      PU1      PU2      WAV      NOI      mix\n");
    for (int b = 0; b < bars; ++b) {
        std::printf("  %3d", b + 1);
        for (int ch = 0; ch < 4; ++ch) std::printf("  %7.5f", channels[size_t(ch)].rms[size_t(b)]);
        std::printf("  %7.5f\n", mix.rms[size_t(b)]);
    }
    std::printf("  peak");
    for (int ch = 0; ch < 4; ++ch) std::printf("  %7.5f", channels[size_t(ch)].peak);
    std::printf("  %7.5f\n", mix.peak);

    bool ok = true;
    for (int ch = 0; ch < 5; ++ch) {
        const SongRun& r = ch < 4 ? channels[size_t(ch)] : mix;
        if (!r.bad) continue;
        std::printf("FAIL %s: a block produced a NaN or an infinity (block %d, sample %d)\n",
                    kStreamName[ch], r.badBlock, r.badBlock * kBlock);
        ok = false;
    }
    if (mix.peak < kSilence) {
        std::printf("FAIL the whole run is silent (peak %g)\n", mix.peak);
        ok = false;
    }
    for (int ch = 0; ch < 4; ++ch) {
        const SongRun& r = channels[size_t(ch)];
        if (r.hi - r.lo >= kSilence) continue;
        std::printf("FAIL %s never changes across the run: it played nothing at all, or its DAC stood still (range %g)\n",
                    kStreamName[ch], r.hi - r.lo);
        ok = false;
    }
    std::printf("\n%s %s over %d bars\n", ok ? "PASSED" : "FAILED",
                file.getFileName().toRawUTF8(), bars);
    return ok ? 0 : 1;
}


/* ------------------------------------------ --remake-midi (section 225) */

/// A song's cells as MIDI through the map: every cell the Player would fire
/// becomes a note-on whose velocity picks the region that carries the cell's
/// INS, TBL and commands; a cell without a note is a note in the command
/// octave (note 0) in such a region; an OFF is a note-off. The timing the
/// Player gives the cells -- grooves, `G`, `H` hops, the chain's loops -- is
/// baked into the ticks, so `G` and `H` are not carried and need not be.
struct RemakeEvent {
    int64_t tick = 0;
    int     order = 0;            ///< at one tick: offs, then row-only notes, then note-ons
    int     ch = 0;
    int     kind = 0;             ///< 0 note-off, 1 note-on, 2 a row without a note (the command octave)
    uint8_t note = 0;
    uint8_t inst = 0, table = 0;
    uint8_t kitA = 0, kitB = 0;   ///< a kit row's NOTE and VEL: its two samples (D-UI-34)
    int8_t  tsp = 0;              ///< a kit row's chain transpose, which its note cannot carry
    bank::Command c1, c2;
};

struct RegionKey {
    uint8_t inst = 0, table = 0; uint8_t kitA = 0, kitB = 0; int8_t tsp = 0; bank::Command c1, c2;
    bool operator==(const RegionKey& o) const { return inst == o.inst && table == o.table && kitA == o.kitA && kitB == o.kitB && tsp == o.tsp && bank::sameCmd(c1, o.c1) && bank::sameCmd(c2, o.c2); }
    bool blank() const { return inst == 0 && table == 0 && c1.cmd == bank::Cmd::None && c2.cmd == bank::Cmd::None; }
};

/// The chain row's transpose baked into the MIDI note (section 225, the user's
/// choice over a region column): the driver's own law -- under the
/// instrument's Transpose flag, PU2's own transpose beside it, and under the
/// channel's floor up by octaves (section 217). A noise index below the map's
/// bottom cannot be a MIDI note and is reported.
int bakedNote(int note, int tsp, int ch, const bank::Instrument* inst, int& belowMap)
{
    if (inst == nullptr || !inst->transpose) return note;
    if (inst->type == bank::InstrumentType::Noise || inst->type == bank::InstrumentType::Kit) {
        const int n = note + tsp;
        if (n < 1) { ++belowMap; return 1; }
        return std::min(n, 127);
    }
    const int instTsp = ch == 1 && inst->type == bank::InstrumentType::Pulse ? int(inst->pu2Transpose) : 0;
    const int floorNote = driver::Driver::lowestNote(ch == 2);
    int t = tsp;
    if (note >= floorNote) while (note + t + instTsp < floorNote && t < 116) t += 12;
    return std::clamp(note + t, 1, 127);
}

int remakeMidi(const juce::File& songFile, const juce::File& outDir)
{
    if (!songFile.existsAsFile()) { std::printf("FAIL %s does not exist\n", songFile.getFullPathName().toRawUTF8()); return 1; }
    auto song = std::make_unique<tracker::Song>();
    auto bank = std::make_unique<bank::Bank>();
    SongReport report;
    if (!loadSong(songFile, *song, report, nullptr, bank.get())) { std::printf("FAIL cannot open %s as a song file\n", songFile.getFullPathName().toRawUTF8()); return 1; }
    if (!report.hasBank) { std::printf("FAIL %s carries no bank\n", songFile.getFullPathName().toRawUTF8()); return 1; }
    for (auto& n : song->noteSource) n = tracker::NoteSource::Tracker;
    tracker::buildRowTables(*song);
    const int64_t end = tracker::songTicks(*song);
    if (song->rows() == 0 || end <= 0) { std::printf("FAIL %s has no rows\n", songFile.getFileName().toRawUTF8()); return 1; }

    // --- the cells as the Player fires them, tick by tick -------------------
    std::vector<RemakeEvent> evs;
    {
        tracker::Player player;
        player.prepare(kSampleRate);
        player.setSong(song.get());
        std::vector<driver::NoteEvent> out;
        uint8_t sounding[4] = { 0, 0, 0, 0 }, lastInst[4] = { 0, 0, 0, 0 };
        int zCells = 0, dropped = 0, belowMap = 0;
        for (int64_t t = 0; t < end; ++t) {
            const driver::TickPoint tp{ 0, t };
            out.clear();
            player.process(&tp, 1, true, out);
            for (const auto& e : out) {
                const int ch = e.channel & 3;
                if (const char* dbg = std::getenv("CHIPBOY_REMAKE_DEBUG"); dbg != nullptr) {
                    int dch = 0; long long from = 0, to = 0;
                    if (std::sscanf(dbg, "%d:%lld:%lld", &dch, &from, &to) == 3 && ch == dch && t >= from && t <= to)
                        std::printf("  tick %lld ch %d kind %d note %d b %d inst %d table %d tsp %d cmd1 %c%d,%d cmd2 %c%d,%d\n", (long long) t, ch, int(e.kind), int(e.a), int(e.b), int(e.inst), int(e.table), int(e.transpose),
                                    e.cmd1.cmd == bank::Cmd::None ? '-' : bank::cmdLetter(e.cmd1.cmd)[0], int(e.cmd1.a), int(e.cmd1.b), e.cmd2.cmd == bank::Cmd::None ? '-' : bank::cmdLetter(e.cmd2.cmd)[0], int(e.cmd2.a), int(e.cmd2.b));
                }
                RegionKey key;
                key.inst = e.inst; key.table = e.table;
                if (e.inst) lastInst[ch] = e.inst;
                const bank::Instrument* inst = bank->instrument(e.inst ? int(e.inst) : int(lastInst[ch]));
                const bool kit = inst != nullptr && inst->type == bank::InstrumentType::Kit;
                if (e.kind == driver::NoteEvent::NoteOn && e.b) {
                    // A kit row: the second sample rides in the region (D-UI-34).
                    // The first stays the MIDI note -- a kit note picks the
                    // sample *and* sets its rate, so the cell's number is kept
                    // and the chain's transpose rides in the region instead.
                    if (kit && e.b != tracker::kDefaultVelocity) key.kitB = e.b;
                    if (kit) key.tsp = e.transpose;
                }
                key.c1 = tracker::midiRegionCommand(e.cmd1); key.c2 = tracker::midiRegionCommand(e.cmd2);
                if ((e.cmd1.cmd != bank::Cmd::None && key.c1.cmd == bank::Cmd::None && e.cmd1.cmd != bank::Cmd::H)
                    || (e.cmd2.cmd != bank::Cmd::None && key.c2.cmd == bank::Cmd::None && e.cmd2.cmd != bank::Cmd::H)) ++dropped;
                if (key.c1.cmd == bank::Cmd::Z || key.c2.cmd == bank::Cmd::Z) ++zCells;
                if (e.kind == driver::NoteEvent::NoteOn && e.b) {
                    // The chain row's transpose is baked into the note (section 225).
                    const int note = kit ? int(e.a) : bakedNote(int(e.a), int(e.transpose), ch, inst, belowMap);
                    if (sounding[ch]) { RemakeEvent off; off.tick = t; off.order = 0; off.ch = ch; off.kind = 0; off.note = sounding[ch]; evs.push_back(off); }
                    RemakeEvent on; on.tick = t; on.order = 2; on.ch = ch; on.kind = 1; on.note = uint8_t(note);
                    on.inst = key.inst; on.table = key.table; on.kitA = key.kitA; on.kitB = key.kitB; on.tsp = key.tsp; on.c1 = key.c1; on.c2 = key.c2;
                    evs.push_back(on);
                    sounding[ch] = uint8_t(note);
                } else if (e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && !e.b)) {
                    if (sounding[ch]) { RemakeEvent off; off.tick = t; off.order = 0; off.ch = ch; off.kind = 0; off.note = sounding[ch]; evs.push_back(off); sounding[ch] = 0; }
                    if (!key.blank()) { RemakeEvent row; row.tick = t; row.order = 1; row.ch = ch; row.kind = 2; row.inst = key.inst; row.table = key.table; row.c1 = key.c1; row.c2 = key.c2; evs.push_back(row); }
                } else if (e.kind == driver::NoteEvent::Command && !e.hybrid) {
                    if (key.blank()) continue;
                    RemakeEvent row; row.tick = t; row.order = 1; row.ch = ch; row.kind = 2; row.inst = key.inst; row.table = key.table; row.c1 = key.c1; row.c2 = key.c2; evs.push_back(row);
                }
            }
        }
        std::printf("%s: %lld ticks, %d events from the cells", songFile.getFileNameWithoutExtension().toRawUTF8(), (long long) end, int(evs.size()));
        if (zCells) std::printf("; %d rows carry a Z, which rolls afresh on every play", zCells);
        if (dropped) std::printf("; %d commands a region cannot carry were dropped", dropped);
        if (belowMap) std::printf("; %d noise notes transposed below the map were clamped", belowMap);
        std::printf("\n");
    }

    // --- the regions: a row's columns once per ChipBoy channel --------------
    std::array<std::vector<RegionKey>, 4> keys;
    std::vector<int> regionOf(evs.size(), -1);
    for (size_t i = 0; i < evs.size(); ++i) {
        const auto& e = evs[i];
        if (e.kind == 0) continue;
        RegionKey k; k.inst = e.inst; k.table = e.table; k.kitA = e.kitA; k.kitB = e.kitB; k.tsp = e.tsp; k.c1 = e.c1; k.c2 = e.c2;
        auto& list = keys[size_t(e.ch)];
        int idx = -1;
        for (size_t r = 0; r < list.size(); ++r) if (list[r] == k) { idx = int(r); break; }
        if (idx < 0) { list.push_back(k); idx = int(list.size()) - 1; }
        regionOf[i] = idx;
    }
    // MIDI channels: each ChipBoy channel takes as many as its regions need,
    // a region a velocity (section 225), in order PU1 PU2 WAV NOI.
    tracker::MidiMap map;
    map.on = true;
    std::array<int, 4> firstMidi{ { -1, -1, -1, -1 } };
    int nextMidi = 0;
    for (int ch = 0; ch < 4; ++ch) {
        const int n = int(keys[size_t(ch)].size());
        if (n == 0) continue;
        const int need = (n + tracker::kMaxRegions - 1) / tracker::kMaxRegions;
        if (nextMidi + need > tracker::kMidiChannels) {
            std::printf("FAIL %s needs more MIDI channels than a port has: PU1 %d, PU2 %d, WAV %d, NOI %d distinct rows at %d a channel\n",
                        songFile.getFileName().toRawUTF8(), int(keys[0].size()), int(keys[1].size()), int(keys[2].size()), int(keys[3].size()), tracker::kMaxRegions);
            return 1;
        }
        firstMidi[size_t(ch)] = nextMidi;
        for (int m = 0; m < need; ++m) {
            auto& c = map.channels[size_t(nextMidi + m)];
            c.target = int8_t(ch);
            c.regions.clear();
            for (int r = m * tracker::kMaxRegions; r < std::min(n, (m + 1) * tracker::kMaxRegions); ++r) {
                const auto& k = keys[size_t(ch)][size_t(r)];
                tracker::MidiRegion reg;
                reg.from = uint8_t(r - m * tracker::kMaxRegions + 1);
                reg.inst = k.inst; reg.table = k.table; reg.kitA = k.kitA; reg.kitB = k.kitB; reg.transpose = k.tsp; reg.cmd1 = k.c1; reg.cmd2 = k.c2;
                c.regions.push_back(reg);
            }
        }
        nextMidi += need;
    }
    tracker::normalizeMidiMap(map);

    // --- the song file: the cells kept for the A/B, the map on, MIDI playing --
    outDir.createDirectory();
    const juce::String stem = songFile.getFileNameWithoutExtension();
    {
        auto out = std::make_unique<tracker::Song>(*song);
        out->midiMap = map;
        for (auto& n : out->noteSource) n = tracker::NoteSource::PianoRoll;
        out->songStartSeconds = 0.0;   // under a host play head tick 0 is the host's beat 0 (section 4)
        const juce::File f = outDir.getChildFile(stem + "-remake.cbsong");
        if (!saveSong(*out, *bank, f, report.bankName)) { std::printf("FAIL cannot write %s\n", f.getFullPathName().toRawUTF8()); return 1; }
        std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
    }

    // --- the MIDI file: 96 a quarter, every event a 96th before its tick ------
    // Under the header's Quantize the plugin holds it to that tick exactly; an
    // event on the tick itself could land a whole tick late in a host that
    // rounds. Tick 0's events sit on the host's beat 0, where the cells' do.
    {
        constexpr int kPpq = 96, kPerTick = kPpq / driver::kTicksPerBeat;   // 4 MIDI ticks a ChipBoy tick
        auto mt = [](int64_t tick) { return uint32_t(std::max<int64_t>(0, tick * kPerTick - 1)); };
        std::vector<uint8_t> file;
        auto be16 = [&](uint32_t v) { file.push_back(uint8_t(v >> 8)); file.push_back(uint8_t(v)); };
        auto be32 = [&](uint32_t v) { file.push_back(uint8_t(v >> 24)); file.push_back(uint8_t(v >> 16)); file.push_back(uint8_t(v >> 8)); file.push_back(uint8_t(v)); };
        auto vlq = [](std::vector<uint8_t>& o, uint32_t v) { uint8_t buf[5]; int n = 0; do { buf[n++] = uint8_t(v & 0x7f); v >>= 7; } while (v && n < 5); while (n > 0) { --n; o.push_back(uint8_t(buf[n] | (n > 0 ? 0x80 : 0))); } };
        struct Ev { uint32_t tick; int order; std::vector<uint8_t> bytes; };
        auto writeTrack = [&](std::vector<Ev>& evsOut, uint32_t endTick) {
            std::stable_sort(evsOut.begin(), evsOut.end(), [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
            std::vector<uint8_t> body; uint32_t at = 0;
            for (const auto& e : evsOut) { const uint32_t t = std::max(at, e.tick); vlq(body, t - at); at = t; body.insert(body.end(), e.bytes.begin(), e.bytes.end()); }
            vlq(body, endTick > at ? endTick - at : 0); body.push_back(0xFF); body.push_back(0x2F); body.push_back(0x00);
            file.push_back('M'); file.push_back('T'); file.push_back('r'); file.push_back('k'); be32(uint32_t(body.size())); file.insert(file.end(), body.begin(), body.end());
        };
        auto meta = [&](std::vector<Ev>& to, uint32_t tick, uint8_t type, const std::vector<uint8_t>& data) { Ev e; e.tick = tick; e.order = 0; e.bytes = { 0xFF, type }; vlq(e.bytes, uint32_t(data.size())); e.bytes.insert(e.bytes.end(), data.begin(), data.end()); to.push_back(std::move(e)); };
        auto text = [&](std::vector<Ev>& to, uint32_t tick, uint8_t type, const juce::String& s) { const auto u = s.toStdString(); meta(to, tick, type, std::vector<uint8_t>(u.begin(), u.end())); };
        const uint32_t endTick = mt(end) + 1;
        std::vector<std::vector<Ev>> tracks{ size_t(nextMidi) };
        std::vector<Ev> zero;
        text(zero, 0, 0x03, stem + " remake");
        {
            const double bpm = std::clamp(song->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
            const uint32_t us = uint32_t(std::clamp(std::llround(60.0e6 / bpm), 1LL, 0xFFFFFFLL));
            meta(zero, 0, 0x51, { uint8_t(us >> 16), uint8_t(us >> 8), uint8_t(us) });
            meta(zero, 0, 0x58, { 4, 2, 24, 8 });
            text(zero, 0, 0x01, "Plays " + stem + "-remake.cbsong through its MIDI map with Tempo source Host at " + juce::String(bpm, 1)
                                + " BPM and Quantize on; every event sits a 96th before its tick. Switch a channel's lane to Trkr to hear the cells instead.");
        }
        for (int m = 0; m < nextMidi; ++m) {
            const auto& c = map.channels[size_t(m)];
            int part = m - firstMidi[size_t(c.target)], parts = 0;
            for (int k = 0; k < nextMidi; ++k) if (map.channels[size_t(k)].target == c.target) ++parts;
            text(tracks[size_t(m)], 0, 0x03, juce::String(kStreamName[c.target]) + (parts > 1 ? " " + juce::String(part + 1) + "/" + juce::String(parts) : juce::String()));
        }
        int notes = 0, rows = 0;
        for (size_t i = 0; i < evs.size(); ++i) {
            const auto& e = evs[i];
            const int ch = e.ch;
            if (e.kind == 0) {
                // The note-off goes on the MIDI channel the note went out on: it
                // was the same voice's, so any of that voice's channels ends it.
                const int m = firstMidi[size_t(ch)];
                Ev ev; ev.tick = mt(e.tick); ev.order = 1; ev.bytes = { uint8_t(0x80 | m), e.note, 0 };
                tracks[size_t(m)].push_back(std::move(ev));
                continue;
            }
            const int r = regionOf[i];
            const int m = firstMidi[size_t(ch)] + r / tracker::kMaxRegions;
            const uint8_t vel = uint8_t(r % tracker::kMaxRegions + 1);
            const uint8_t note = e.kind == 1 ? e.note : uint8_t(0);
            Ev on; on.tick = mt(e.tick); on.order = e.kind == 1 ? 3 : 2; on.bytes = { uint8_t(0x90 | m), note, vel };
            tracks[size_t(m)].push_back(std::move(on));
            if (e.kind == 2) { Ev off; off.tick = mt(e.tick) + 2; off.order = 1; off.bytes = { uint8_t(0x80 | m), 0, 0 }; tracks[size_t(m)].push_back(std::move(off)); ++rows; }
            else ++notes;
        }
        file.push_back('M'); file.push_back('T'); file.push_back('h'); file.push_back('d');
        be32(6); be16(1); be16(uint32_t(1 + nextMidi)); be16(kPpq);
        writeTrack(zero, endTick);
        for (auto& t : tracks) writeTrack(t, endTick);
        const juce::File f = outDir.getChildFile(stem + "-remake.mid");
        if (!f.replaceWithData(file.data(), file.size())) { std::printf("FAIL cannot write %s\n", f.getFullPathName().toRawUTF8()); return 1; }
        std::printf("wrote %s: %d notes, %d rows without a note, on %d MIDI channels\n", f.getFullPathName().toRawUTF8(), notes, rows, nextMidi);
        for (int ch = 0; ch < 4; ++ch) if (firstMidi[size_t(ch)] >= 0)
            std::printf("  %s: %d distinct rows on MIDI channel%s %d%s\n", kStreamName[ch], int(keys[size_t(ch)].size()),
                        int(keys[size_t(ch)].size()) > tracker::kMaxRegions ? "s" : "", firstMidi[size_t(ch)] + 1,
                        int(keys[size_t(ch)].size()) > tracker::kMaxRegions ? ("-" + juce::String(firstMidi[size_t(ch)] + (int(keys[size_t(ch)].size()) + tracker::kMaxRegions - 1) / tracker::kMaxRegions)).toRawUTF8() : "");
    }
    return 0;
}

/// `--check-remake SONG.cbsong FILE.mid [bars]`: the song's cells under a host
/// play head at the song's tempo, then the MIDI file through the song's map,
/// and the two register streams compared write for write (section 9.5's
/// tolerance). The remake's proof. Without `bars` it runs the song's own
/// length in whole bars: past its end the cells come round again (section
/// 212) and the MIDI file has stopped, which is no difference of the map's.
int checkRemake(const juce::File& songFile, const juce::File& midiFile, int bars)
{
    if (!songFile.existsAsFile() || !midiFile.existsAsFile()) { std::printf("FAIL a file is missing\n"); return 1; }
    double tempo = 120.0;
    if (bars <= 0) {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        SongReport report;
        if (!openForPlayback(*pOwned, songFile, -1, report)) return 1;
        const auto song = pOwned->song();
        bars = song ? int(tracker::songTicks(*song) / (4 * driver::kTicksPerBeat)) : 8;
        bars = std::max(1, bars);
    }
    auto pass = [&](bool viaMidi, Capture& cap) {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        auto& p = *pOwned;
        SongReport report;
        if (!openForPlayback(p, songFile, -1, report)) return false;
        const auto song = p.song();
        if (song == nullptr) return false;
        tempo = std::clamp(song->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
        p.mutateSong([viaMidi](tracker::Song& s) { for (auto& n : s.noteSource) n = viaMidi ? tracker::NoteSource::PianoRoll : tracker::NoteSource::Tracker; });
        std::vector<TimedMessage> midi;
        if (viaMidi && !loadMidi(midiFile, tempo, midi)) { std::printf("FAIL cannot read %s\n", midiFile.getFullPathName().toRawUTF8()); return false; }
        p.prepareToPlay(kSampleRate, kBlock);
        setParameter(p, ids::notesOnTick, 1.0);
        setParameter(p, ids::tempoSource, 0.0);
        std::vector<driver::RegWrite> log;
        log.reserve(1u << 20);
        p.setWriteLog(&log);
        FakePlayHead head;
        head.bpm = tempo;
        p.setPlayHead(&head);
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer in;
        const int64_t samples = int64_t(std::llround(double(bars) * 4.0 * 60.0 / tempo * kSampleRate));
        const int blocks = int((samples + kBlock - 1) / kBlock);
        size_t next = 0;
        for (int b = 0; b < blocks; ++b) {
            const int64_t f0 = int64_t(b) * kBlock;
            head.frame = f0;
            in.clear();
            while (next < midi.size() && midi[next].sample < f0 + kBlock) { in.addEvent(midi[next].message, int(midi[next].sample - f0)); ++next; }
            p.processBlock(buffer, in);
            // CHIPBOY_VIEW=ch:fromSample:toSample prints the channel's voice view after each block in the window.
            if (const char* vw = std::getenv("CHIPBOY_VIEW"); vw != nullptr) {
                int vch = 0; long long from = 0, to = 0;
                if (std::sscanf(vw, "%d:%lld:%lld", &vch, &from, &to) == 3 && f0 >= from && f0 <= to) {
                    const auto& v = p.driverView().view(vch & 3);
                    std::printf("  %s block@%lld active %d note %d period %d pitchOff %d table %d row %d vol %d rng %08x\n", viaMidi ? "map  " : "cells", (long long) f0,
                                int(v.active), int(v.note), int(v.period), int(v.pitchOffset), int(v.tableSlot), int(v.tableRow), int(v.volume), unsigned(v.rng));
                }
            }
            if (b % 16 == 0) pump(1);
        }
        p.setWriteLog(nullptr);
        p.setPlayHead(nullptr);
        for (const auto& w : log) {
            const int s = streamOf(w.addr);
            if (s < 0) continue;
            cap.streams[size_t(s)].push_back({ int64_t(std::llround(double(w.cycle) * kSampleRate / double(kCpuHz))), w.cycle, w.addr, w.value });
        }
        return true;
    };
    Capture cells, viaMap;
    if (!pass(false, cells)) return 1;
    if (!pass(true, viaMap)) return 1;
    std::printf("%s over %d bars at %.0f BPM: the cells (record) against the MIDI through the map (replay)\n", songFile.getFileNameWithoutExtension().toRawUTF8(), bars, tempo);
    // CHIPBOY_DUMP=stream:fromSample:count prints both passes' writes of one stream from a sample.
    if (const char* d = std::getenv("CHIPBOY_DUMP"); d != nullptr) {
        int s = 0; long long from = 0; int count = 40;
        if (std::sscanf(d, "%d:%lld:%d", &s, &from, &count) >= 2) {
            for (int side = 0; side < 2; ++side) {
                const auto& st = (side == 0 ? cells : viaMap).streams[size_t(s & 3)];
                std::printf("--- %s\n", side == 0 ? "cells" : "map");
                int printed = 0;
                for (const auto& w : st) if (w.sample >= from && printed < count) { std::printf("  %s\n", writeText(w, tempo).toRawUTF8()); ++printed; }
            }
        }
    }
    for (int s = 0; s < kStreams; ++s) std::printf("  %-6s %6d writes from the cells, %6d through the map\n", kStreamName[s], int(cells.streams[size_t(s)].size()), int(viaMap.streams[size_t(s)].size()));
    const bool same = compare(cells, viaMap, tempo);
    std::printf("\n%s the MIDI remake %s the cells write for write over %d bars\n", same ? "PASSED" : "FAILED", same ? "matches" : "does not match", bars);
    return same ? 0 : 1;
}

/// `--record-midi SONG.cbsong FILE.mid OUT.cbsong [bars]` (section 226): the
/// song file opens in a tab with its map, every channel armed and Rec on, and
/// the MIDI file plays in under a host play head at the song's tempo with
/// Quantize on -- so the rows are recorded, and each row's groove fitted at
/// its end. The song that came out is written, and what each recorded row
/// became is listed: its steps, its groove, its G cells. The groove cases'
/// check, and a way to try a MIDI file against the fitter.
int recordMidi(const juce::File& songFile, const juce::File& midiFile, const juce::File& outFile, int bars, const juce::File& expectFile)
{
    if (!songFile.existsAsFile() || !midiFile.existsAsFile()) { std::printf("FAIL a file is missing\n"); return 1; }
    const auto pOwned = std::make_unique<ChipBoyProcessor>();
    auto& p = *pOwned;
    SongReport report;
    if (!openForPlayback(p, songFile, -1, report)) return 1;
    const auto song0 = p.song();
    if (song0 == nullptr) return 1;
    const double tempo = std::clamp(song0->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
    std::vector<TimedMessage> midi;
    if (!loadMidi(midiFile, tempo, midi)) { std::printf("FAIL cannot read %s\n", midiFile.getFullPathName().toRawUTF8()); return 1; }
    std::printf("%s: %.0f BPM, auto groove %s; recording %s, %d events over %d bars\n", songFile.getFileNameWithoutExtension().toRawUTF8(), tempo,
                song0->autoGroove ? "on" : "off", midiFile.getFileName().toRawUTF8(), int(midi.size()), bars);
    p.mutateSong([](tracker::Song& s) { for (auto& a : s.recordArm) a = true; });
    p.prepareToPlay(kSampleRate, kBlock);
    setParameter(p, ids::notesOnTick, 1.0);
    setParameter(p, ids::tempoSource, 0.0);
    p.setRecordArm(true);
    // CHIPBOY_PUMP_MS: how long the message loop runs after each block, so
    // the 100 ms timer -- which closes the takes -- lands on other blocks; a
    // sweep of it must record the same rows (section 226).
    int pumpMs = 1;
    if (const char* e = std::getenv("CHIPBOY_PUMP_MS"); e != nullptr) pumpMs = std::clamp(std::atoi(e), 0, 500);
    FakePlayHead head;
    head.bpm = tempo;
    p.setPlayHead(&head);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer in;
    const int64_t samples = int64_t(std::llround(double(bars) * 4.0 * 60.0 / tempo * kSampleRate));
    const int blocks = int((samples + kBlock - 1) / kBlock);
    size_t next = 0;
    for (int b = 0; b < blocks; ++b) {
        const int64_t f0 = int64_t(b) * kBlock;
        head.frame = f0;
        in.clear();
        while (next < midi.size() && midi[next].sample < f0 + kBlock) { in.addEvent(midi[next].message, int(midi[next].sample - f0)); ++next; }
        p.processBlock(buffer, in);
        pump(pumpMs);                             // the recorder's messages are applied on the timer
    }
    head.playing = false;
    p.processBlock(buffer, in);                   // a stopped play head: the last take ends
    p.setRecordArm(false);
    pump(400);
    p.setPlayHead(nullptr);
    const auto song = p.song();
    if (song == nullptr) { std::printf("FAIL no song after recording\n"); return 1; }
    if (outFile != juce::File()) {
        if (!p.saveSongFile(outFile)) { std::printf("FAIL cannot write %s\n", outFile.getFullPathName().toRawUTF8()); return 1; }
        std::printf("wrote %s\n", outFile.getFullPathName().toRawUTF8());
    }
    // What each recorded row became.
    std::vector<juce::String> got;
    auto entriesOf = [&](uint8_t slot) {
        if (slot < 1 || slot > tracker::kGrooveSlots) return juce::String("straight");
        const auto& g = song->grooves[size_t(slot - 1)];
        juce::String e;
        for (int i = 0; i < g.length(); ++i) e += (i ? " " : "") + juce::String(int(g.ticks[size_t(i)]));
        return e == "6" || e == "6 6" ? juce::String("straight") : e;
    };
    for (int ch = 0; ch < 4; ++ch) {
        const auto& chain = song->chain[size_t(ch)];
        for (size_t row = 0; row < chain.size(); ++row) {
            const auto* ph = song->phrase(chain[row]);
            if (ph == nullptr) continue;
            int notes = 0; juce::String gs;
            for (int s = 0; s < ph->length(); ++s) {
                const auto& c = ph->cells[size_t(s)];
                if (c.note >= 1 && c.note <= 127) ++notes;
                for (const auto* cmd : { &c.cmd1, &c.cmd2 })
                    if (cmd->cmd == bank::Cmd::G) gs += " G" + (bank::isRevert(*cmd) ? juce::String("=") : juce::String(int(cmd->a))) + "@" + juce::String(s + 1);
            }
            if (notes == 0 && gs.isEmpty()) continue;
            juce::String groove = "straight";
            if (ph->groove >= 1 && ph->groove <= tracker::kGrooveSlots) {
                const auto& g = song->grooves[size_t(ph->groove - 1)];
                groove = juce::String();
                for (int i = 0; i < g.length(); ++i) groove += (i ? " " : "") + juce::String(int(g.ticks[size_t(i)]));
                groove += " (slot " + juce::String(int(ph->groove)) + (g.named() ? juce::String(", ") + juce::String(juce::CharPointer_UTF8(g.nameOf())) : juce::String()) + ")";
            }
            const int64_t start = tracker::rowStartTick(*song, ch, int(row)), len = tracker::rowStartTick(*song, ch, int(row) + 1) - start;
            std::printf("  %s row %2d @%5lld +%3lld: phrase %3d, %2d steps, groove %s, %d notes%s\n", kStreamName[ch], int(row + 1), (long long) start, (long long) len, int(chain[row]), ph->length(), groove.toRawUTF8(), notes, gs.toRawUTF8());
            // The row in the expect file's words: the grooves by their entries,
            // not their slots, which depend on what was free.
            juce::String canon = juce::String(kStreamName[ch]) + " row " + juce::String(int(row + 1)) + ": " + juce::String(ph->length()) + " steps, " + entriesOf(ph->groove) + ", " + juce::String(notes) + " notes";
            for (int s = 0; s < ph->length(); ++s)
                for (const auto* cmd : { &ph->cells[size_t(s)].cmd1, &ph->cells[size_t(s)].cmd2 })
                    if (cmd->cmd == bank::Cmd::G) canon += ", G " + (bank::isRevert(*cmd) ? juce::String("=") : entriesOf(uint8_t(cmd->a))) + " @" + juce::String(s + 1);
            got.push_back(canon);
        }
    }
    if (expectFile != juce::File()) {
        juce::StringArray want;
        want.addLines(expectFile.loadFileAsString());
        want.removeEmptyStrings();
        for (auto& w : want) w = w.trim();
        for (int k = want.size(); --k >= 0;) if (want[k].startsWith("#")) want.remove(k);
        int bad = 0;
        for (const auto& w : want) if (std::find(got.begin(), got.end(), w) == got.end()) { std::printf("  missing: %s\n", w.toRawUTF8()); ++bad; }
        for (const auto& g : got) if (!want.contains(g)) { std::printf("  unexpected: %s\n", g.toRawUTF8()); ++bad; }
        if (bad) { std::printf("FAILED %d rows differ from %s\n", bad, expectFile.getFileName().toRawUTF8()); return 1; }
        std::printf("  every row as %s expects\n", expectFile.getFileName().toRawUTF8());
    }
    std::printf("PASSED recorded %s into %s\n", midiFile.getFileName().toRawUTF8(), songFile.getFileName().toRawUTF8());
    return 0;
}

/// `chipboy_recordtest --play-midi SONG.cbsong FILE.mid [bars]` (section 225):
/// the song file opens in a tab -- its bank and its MIDI map -- and the MIDI
/// file plays into the plugin under a host play head at the song's tempo, as
/// a DAW would send it. What comes out is measured as --play-song measures a
/// song: the mix and each soloed channel, a NaN, silence or a channel that
/// never moves failing it. The demo's check, and a way to hear a map.
int playMidi(const juce::File& songFile, const juce::File& midiFile, int bars)
{
    constexpr double kSilence = 1.0e-4;
    if (!songFile.existsAsFile()) { std::printf("FAIL %s does not exist\n", songFile.getFullPathName().toRawUTF8()); return 1; }
    if (!midiFile.existsAsFile()) { std::printf("FAIL %s does not exist\n", midiFile.getFullPathName().toRawUTF8()); return 1; }
    double tempo = 120.0;
    int mapped = 0;
    bool on = false;
    {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        SongReport report;
        if (!openForPlayback(*pOwned, songFile, -1, report)) return 1;
        const auto song = pOwned->song();
        if (song == nullptr) { std::printf("FAIL %s opened with no song\n", songFile.getFullPathName().toRawUTF8()); return 1; }
        tempo = std::clamp(song->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
        on = song->midiMap.on;
        for (const auto& c : song->midiMap.channels) if (c.target >= 0) ++mapped;
        std::printf("%s: %.0f BPM, bank \"%s\", MIDI map %s with %d channels assigned\n", songFile.getFileNameWithoutExtension().toRawUTF8(),
                    tempo, report.bankName.toRawUTF8(), on ? "on" : "off", mapped);
    }
    std::vector<TimedMessage> midi;
    if (!loadMidi(midiFile, tempo, midi)) { std::printf("FAIL cannot read %s as a MIDI file\n", midiFile.getFullPathName().toRawUTF8()); return 1; }
    std::printf("plays %s: %d events over %d bars at %.0f BPM\n", midiFile.getFileName().toRawUTF8(), int(midi.size()), bars, tempo);

    const double samplesPerBar = 4.0 * 60.0 / tempo * kSampleRate;
    const int64_t samples = int64_t(std::llround(double(bars) * samplesPerBar));
    const int blocks = int((samples + kBlock - 1) / kBlock);
    auto once = [&](int solo, SongRun& out) {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        auto& p = *pOwned;
        SongReport report;
        if (!openForPlayback(p, songFile, solo, report)) return false;
        p.prepareToPlay(kSampleRate, kBlock);
        if (gConsole >= 0) setParameter(p, ids::model, double(gConsole));
        FakePlayHead head;
        head.bpm = tempo;
        p.setPlayHead(&head);
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer in;
        std::vector<double> sum(size_t(bars), 0.0), count(size_t(bars), 0.0);
        out.rms.assign(size_t(bars), 0.0);
        size_t next = 0;
        for (int b = 0; b < blocks; ++b) {
            const int64_t f0 = int64_t(b) * kBlock;
            head.frame = f0;
            in.clear();
            while (next < midi.size() && midi[next].sample < f0 + kBlock) { in.addEvent(midi[next].message, int(midi[next].sample - f0)); ++next; }
            p.processBlock(buffer, in);
            for (int i = 0; i < kBlock; ++i) {
                const int64_t f = f0 + i;
                if (f >= samples) break;
                const size_t bar = size_t(std::min<int64_t>(int64_t(double(f) / samplesPerBar), int64_t(bars) - 1));
                for (int c = 0; c < buffer.getNumChannels(); ++c) {
                    const double v = double(buffer.getSample(c, i));
                    if (!std::isfinite(v)) { if (!out.bad) out.badBlock = b; out.bad = true; continue; }
                    sum[bar] += v * v; count[bar] += 1.0;
                    out.lo = std::min(out.lo, v); out.hi = std::max(out.hi, v); out.peak = std::max(out.peak, std::fabs(v));
                }
            }
            if (b % 16 == 0) pump(1);
        }
        p.setPlayHead(nullptr);
        pump(50);
        for (size_t i = 0; i < size_t(bars); ++i) out.rms[i] = count[i] > 0.0 ? std::sqrt(sum[i] / count[i]) : 0.0;
        return true;
    };
    SongRun mix;
    std::array<SongRun, 4> channels;
    if (!once(-1, mix)) return 1;
    for (int ch = 0; ch < 4; ++ch) if (!once(ch, channels[size_t(ch)])) return 1;

    std::printf("\nRMS per bar\n  bar      PU1      PU2      WAV      NOI      mix\n");
    for (int b = 0; b < bars; ++b) {
        std::printf("  %3d", b + 1);
        for (int ch = 0; ch < 4; ++ch) std::printf("  %7.5f", channels[size_t(ch)].rms[size_t(b)]);
        std::printf("  %7.5f\n", mix.rms[size_t(b)]);
    }
    bool ok = true;
    for (int ch = 0; ch < 5; ++ch) {
        const SongRun& r = ch < 4 ? channels[size_t(ch)] : mix;
        if (!r.bad) continue;
        std::printf("FAIL %s: a block produced a NaN or an infinity (block %d)\n", kStreamName[ch], r.badBlock);
        ok = false;
    }
    if (mix.peak < kSilence) { std::printf("FAIL the whole run is silent (peak %g)\n", mix.peak); ok = false; }
    for (int ch = 0; ch < 4; ++ch) {
        const SongRun& r = channels[size_t(ch)];
        if (r.hi - r.lo >= kSilence) continue;
        std::printf("FAIL %s never changes across the run: the map never reached it, or its DAC stood still (range %g)\n", kStreamName[ch], r.hi - r.lo);
        ok = false;
    }
    if (!on || mapped == 0) { std::printf("FAIL the song's MIDI map is off or assigns nothing\n"); ok = false; }
    std::printf("\n%s %s through %s over %d bars\n", ok ? "PASSED" : "FAILED", midiFile.getFileName().toRawUTF8(), songFile.getFileName().toRawUTF8(), bars);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::File demoDir = juce::File(CHIPBOY_DEMO_DIR);
    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile("recordtest");
    juce::File writeSong, checkSong, writeState, checkState, playFile;
    juce::File importSav, importOut; juce::String importWhich;   // --import-sav SAV NAME|working OUT.cbsong (a .lsdprj too)
    juce::String importModel;                                    // --model NAME: read the song as that version rather than the format's default
    juce::File traceFile, traceOut; double traceSeconds = 20.0;  // --trace-song FILE OUT.csv [seconds]
    double traceBpm = 0.0;                                       // --tempo BPM: play it at this tempo instead
    int64_t traceFrom = 0;                                       // --from TICK: locate there before playing (section 223)
    juce::File midiFile, midiOut; bool midiNoise = true;         // --export-midi FILE OUT.mid [--no-noise] (section 224)
    juce::File playMidiSong, playMidiFile; int playMidiBars = 8;  // --play-midi SONG.cbsong FILE.mid [bars] (section 225)
    juce::File remakeSong, remakeOut;                             // --remake-midi SONG.cbsong OUTDIR (section 225)
    juce::File recSong, recMidi, recOut, recExpect; int recBars = 16;   // --record-midi SONG.cbsong FILE.mid OUT.cbsong [bars] [--expect FILE] (section 226)
    juce::File checkSongFile, checkMidiFile; int checkBars = 0;   // --check-remake SONG.cbsong FILE.mid [bars]; 0: the song's own length
    int playBars = 8;                                         // --play-song's default (section 24)
    bool dump = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String key(argv[i]);
        if (key == "--dump") dump = true;                     // every write, to diff by hand
        else if (key == "--no-noise") midiNoise = false;      // --export-midi's option (section 224)
        else if (key == "--expect" && i + 1 < argc) recExpect = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);   // --record-midi's check (section 226)
        else if (i + 1 >= argc) continue;
        else if (key == "--demo") demoDir = juce::File(juce::String(argv[++i]));
        else if (key == "--out") outDir = juce::File(juce::String(argv[++i]));
        else if (key == "--write-song") writeSong = juce::File(juce::String(argv[++i]));
        else if (key == "--wav" && i + 1 < argc) gWavDir = juce::File(juce::String(argv[++i]));
        else if (key == "--rate" && i + 1 < argc) gSampleRate = std::strtod(argv[++i], nullptr);
        else if (key == "--console" && i + 1 < argc) { const juce::String c = juce::String(argv[++i]).toLowerCase(); gConsole = c == "dmg" ? 0 : c == "cgb" ? 1 : c == "raw" ? 2 : -1; }
        else if (key == "--block" && i + 1 < argc) gBlock = int(std::strtol(argv[++i], nullptr, 10));
        else if (key == "--check-song") checkSong = juce::File(juce::String(argv[++i]));
        else if (key == "--write-state") writeState = juce::File(juce::String(argv[++i]));
        else if (key == "--check-state") checkState = juce::File(juce::String(argv[++i]));
        else if (key == "--model") importModel = juce::String(argv[++i]);
        else if (key == "--tempo") traceBpm = std::clamp(juce::String(argv[++i]).getDoubleValue(), driver::kMinSongBpm, driver::kMaxSongBpm);
        else if (key == "--from" && i + 1 < argc) traceFrom = std::max<int64_t>(0, juce::String(argv[++i]).getLargeIntValue());
        else if (key == "--record-midi" && i + 3 < argc) {
            recSong = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            recMidi = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            recOut = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-') recBars = std::clamp(int(std::strtol(argv[++i], nullptr, 10)), 1, 256);
        }
        else if (key == "--remake-midi" && i + 2 < argc) { remakeSong = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); remakeOut = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); }
        else if (key == "--check-remake" && i + 2 < argc) {
            checkSongFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            checkMidiFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-') checkBars = std::clamp(int(std::strtol(argv[++i], nullptr, 10)), 1, 256);
        }
        else if (key == "--play-midi" && i + 2 < argc) {
            playMidiSong = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            playMidiFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-') playMidiBars = std::clamp(int(std::strtol(argv[++i], nullptr, 10)), 1, 64);
        }
        else if (key == "--export-midi" && i + 2 < argc) { midiFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); midiOut = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); }
        else if (key == "--import-sav" && i + 3 < argc) {
            importSav = juce::File(juce::String(argv[++i])); importWhich = juce::String(argv[++i]); importOut = juce::File(juce::String(argv[++i]));
        }
        else if (key == "--trace-song" && i + 2 < argc) {
            traceFile = juce::File(juce::String(argv[++i])); traceOut = juce::File(juce::String(argv[++i]));
            if (i + 1 < argc && juce::String(argv[i + 1]).containsOnly("0123456789.") && juce::String(argv[i + 1]).isNotEmpty())
                traceSeconds = std::clamp(juce::String(argv[++i]).getDoubleValue(), 1.0, 600.0);
        }
        else if (key == "--play-song") {
            playFile = juce::File(juce::String(argv[++i]));
            // The bar count is optional and follows the file, so it is taken
            // only when the next argument is a plain number.
            if (i + 1 < argc && juce::String(argv[i + 1]).containsOnly("0123456789") && juce::String(argv[i + 1]).isNotEmpty())
                playBars = std::clamp(juce::String(argv[++i]).getIntValue(), 1, 512);
        }
    }
    outDir.createDirectory();

    /* ---- --import-sav: one song of an LSDj save into a .cbsong (section 54) ---- */
    // The plugin's importer from the command line: the same code the dialog
    // runs, so a save can be converted and then played with --play-song.
    if (importSav != juce::File()) {
        plugin::SavePreview preview; juce::String error;
        if (!plugin::readSave(importSav, preview, error)) { std::printf("FAIL %s\n", error.toRawUTF8()); return 1; }
        std::vector<uint8_t> bytes; std::string err; int format = preview.index.workingFormat; juce::String name = importWhich;
        if (!preview.projects.empty()) { bytes = preview.projects.front().song; format = preview.projects.front().formatVersion; name = preview.projects.front().name; }
        else if (importWhich == "working") { lsdj::workingSong(preview.bytes.data(), preview.bytes.size(), bytes); name = "Working song"; }
        else {
            const chipboy::lsdj::SaveEntry* hit = nullptr;
            for (const auto& e : preview.index.files) if (juce::String(e.name).equalsIgnoreCase(importWhich) || juce::String(e.file) == importWhich) hit = &e;
            if (hit == nullptr) {
                std::printf("FAIL no song named %s; the save holds:", importWhich.toRawUTF8());
                for (const auto& e : preview.index.files) std::printf(" %s(%d,f%d)", e.name.c_str(), e.file, e.formatVersion);
                std::printf("\n"); return 1;
            }
            if (!lsdj::decompressFile(preview.bytes.data(), preview.bytes.size(), hit->file, bytes, err)) { std::printf("FAIL %s\n", err.c_str()); return 1; }
            format = hit->formatVersion; name = hit->name;
        }
        // --model names a version's own reading, which is how two releases that
        // write the same format byte are told apart (docs/LSDJ_VERSIONS.md).
        const lsdj::LsdjModel* forced = importModel.isEmpty() ? nullptr : lsdj::lsdjModelNamed(importModel.toRawUTF8());
        if (forced == nullptr && importModel.isNotEmpty()) forced = lsdj::lsdjModelForRomVersion(importModel.toRawUTF8());
        if (forced == nullptr && importModel.isNotEmpty()) {
            std::printf("FAIL no model named %s; the models are:", importModel.toRawUTF8());
            int n = 0; const auto* const* all = lsdj::lsdjModels(n);
            for (int k = 0; k < n; ++k) std::printf("\n  %s", all[k]->name);
            std::printf("\n"); return 1;
        }
        const auto& model = forced != nullptr ? *forced : plugin::autoModel(format, preview.romVersion);
        auto bank = std::make_unique<bank::Bank>(); auto song = std::make_unique<tracker::Song>();
        lsdj::ImportSummary sum; lsdj::ImportNotes notes;
        const std::vector<lsdj::LsdjKit>* kits = !preview.projects.empty() && !preview.projects.front().kits.empty() ? &preview.projects.front().kits   // section 218: the project's own kits first
                                               : preview.kits.empty() ? nullptr : &preview.kits;
        if (!lsdj::importSong(bytes.data(), bytes.size(), model, *bank, *song, sum, notes, kits, &preview.rawPages)) { std::printf("FAIL the song could not be read\n"); return 1; }
        if (!plugin::saveSong(*song, *bank, importOut, "LSDj " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " " + name)) { std::printf("FAIL cannot write %s\n", importOut.getFullPathName().toRawUTF8()); return 1; }
        const bool own = !preview.projects.empty() && !preview.projects.front().kits.empty();
        std::printf("wrote %s: %s, format %d read as %s, %d instruments, %d tables, %d waves, %d kits (%s %s, %d kits in it), %d phrases, %d rows, tempo %.0f, %d notes\n",
                    importOut.getFullPathName().toRawUTF8(), name.toRawUTF8(), format, model.name, sum.instruments, sum.tables, sum.waves, sum.kits,
                    own ? "project" : "ROM", own ? preview.projects.front().file.getFileName().toRawUTF8() : preview.romFile == juce::File() ? "none" : preview.romFile.getFileName().toRawUTF8(),
                    own ? int(preview.projects.front().kitNumbers.size()) : int(preview.kits.size()), sum.phrases, sum.rows, sum.tempoBpm, int(notes.lines.size()));
        for (const auto& l : notes.lines) std::printf("  - %s\n", l.c_str());
        return 0;
    }

    /* ---- --trace-song: the register writes of a song file, as a CSV ---- */
    if (traceFile != juce::File()) return traceSong(traceFile, traceOut, traceSeconds, traceBpm, traceFrom);
    /* ---- --remake-midi / --check-remake: a song's cells as MIDI (section 225) ---- */
    if (remakeSong != juce::File()) return remakeMidi(remakeSong, remakeOut);
    if (recSong != juce::File()) return recordMidi(recSong, recMidi, recOut, recBars, recExpect);
    if (checkSongFile != juce::File()) return checkRemake(checkSongFile, checkMidiFile, checkBars);
    /* ---- --play-midi: a MIDI file through a song's map (section 225) ---- */
    if (playMidiSong != juce::File()) return playMidi(playMidiSong, playMidiFile, playMidiBars);
    /* ---- --export-midi: the song file as a MIDI file (section 224) ---- */
    if (midiFile != juce::File()) {
        auto bank = std::make_unique<bank::Bank>();
        auto song = std::make_unique<tracker::Song>();
        SongReport report;
        if (!plugin::loadSong(midiFile, *song, report, nullptr, bank.get())) { std::printf("FAIL cannot open %s as a song file\n", midiFile.getFullPathName().toRawUTF8()); return 1; }
        midi::Options opt; opt.noise = midiNoise;
        midi::Report rep;
        const auto bytes = midi::exportSong(*song, *bank, opt, &rep);
        if (bytes.empty() || !midiOut.replaceWithData(bytes.data(), bytes.size())) { std::printf("FAIL nothing to write for %s\n", midiFile.getFileName().toRawUTF8()); return 1; }
        std::printf("wrote %s: %d bytes, %lld ticks at %d a quarter, notes PU1 %d PU2 %d WAV %d NOI %d\n", midiOut.getFullPathName().toRawUTF8(), int(bytes.size()),
                    (long long) rep.ticks, rep.ppq, rep.notes[0], rep.notes[1], rep.notes[2], midiNoise ? rep.notes[3] : 0);
        for (const auto& l : rep.notes_) std::printf("  - %s\n", l.c_str());
        return 0;
    }

    /* ---- --play-song: a song file plays, and is heard (section 24) ---- */
    // Nothing under Demo/ is needed for this, so it runs before the demo is
    // read: any song file, anywhere, can be played.
    if (playFile != juce::File()) return playSong(playFile, playBars);

    const juce::File demoSongFile = checkSong != juce::File() ? checkSong : demoDir.getChildFile("ChipBoy Demo.cbsong");

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

    /* ---- the hybrid project's plugin state (section 20) -------------- */
    if (writeState != juce::File() || checkState != juce::File()) {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        auto& p = *pOwned;
        p.setInstanceUuid(kStateUuid);
        p.setInstanceName(kStateName);
        if (!buildHybridProject(p, demoDir.getChildFile("ChipBoy Demo.cbsong"), aut, /*statics*/ true)) return 1;
        juce::MemoryBlock state;
        p.getStateInformation(state);
        const juce::File target = writeState != juce::File() ? writeState : checkState;
        if (writeState != juce::File()) {
            if (!target.replaceWithData(state.getData(), state.getSize())) {
                std::printf("FAIL cannot write %s\n", target.getFullPathName().toRawUTF8());
                return 1;
            }
            std::printf("wrote %s (%d bytes, %d tabs)\n", target.getFullPathName().toRawUTF8(), int(state.getSize()), p.tabCount());
            return 0;
        }
        if (!target.existsAsFile()) { std::printf("FAIL %s does not exist; write it with --write-state\n", target.getFullPathName().toRawUTF8()); return 1; }
        juce::MemoryBlock have;
        target.loadFileAsData(have);
        if (have != state) {
            const juce::File wrote = outDir.getChildFile("hybrid.state");
            wrote.replaceWithData(state.getData(), state.getSize());
            std::printf("FAIL %s is not the state this build writes\n", target.getFullPathName().toRawUTF8());
            std::printf("     the file is %d bytes, this build's is %d; it is at %s\n",
                        int(have.getSize()), int(state.getSize()), wrote.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf("PASSED the hybrid project's state matches (%d bytes)\n", int(state.getSize()));
        return 0;
    }

    /* ---- the MIDI map's round trip through the song file (section 225) -- */
    {
        auto a = std::make_unique<tracker::Song>();
        a->midiMap.on = true;
        a->midiMap.channels[0].target = 0;
        a->midiMap.channels[1].target = 2;
        a->midiMap.channels[1].regions = { tracker::MidiRegion{ 1, 0, 0, 0, 0, 0, { bank::Cmd::V, 4, 6, 0 }, {} }, tracker::MidiRegion{ 65, 7, 2, 2, 3, -4, {}, { bank::Cmd::K, 3, 0, 0 } } };
        a->midiMap.channels[9].regions = { tracker::MidiRegion{ 1, 12, 0, 0, 0, 0, {}, {} }, tracker::MidiRegion{ 90, 15, 0, 0, 0, 0, {}, {} } };   // regions without a target survive too
        auto b = std::make_unique<tracker::Song>();
        if (!songFromJson(songToJson(*a), *b)) { std::printf("FAIL the MIDI map's song does not read back\n"); return 1; }
        bool same = b->midiMap.on == a->midiMap.on;
        for (int m = 0; same && m < tracker::kMidiChannels; ++m) {
            const auto& x = a->midiMap.channels[size_t(m)]; const auto& y = b->midiMap.channels[size_t(m)];
            same = x.target == y.target && x.regions.size() == y.regions.size();
            for (size_t i = 0; same && i < x.regions.size(); ++i)
                same = x.regions[i].from == y.regions[i].from && x.regions[i].inst == y.regions[i].inst && x.regions[i].table == y.regions[i].table
                       && x.regions[i].kitA == y.regions[i].kitA && x.regions[i].kitB == y.regions[i].kitB && x.regions[i].transpose == y.regions[i].transpose
                       && bank::sameCmd(x.regions[i].cmd1, y.regions[i].cmd1) && bank::sameCmd(x.regions[i].cmd2, y.regions[i].cmd2);
        }
        if (!same) { std::printf("FAIL the MIDI map does not survive the song file\n"); return 1; }
        auto c = std::make_unique<tracker::Song>();
        if (!songFromJson(songToJson(*c), *c) || !tracker::midiMapIsDefault(c->midiMap)) { std::printf("FAIL a song without a map reads back with one\n"); return 1; }
        std::printf("PASSED the MIDI map round trip through the song file\n");
    }

    /* ---- pass 1: play the demo in, record it ------------------------- */
    juce::String recorded, songFile;
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
        // The song file: the recording, with the bank it plays through
        // (docs/COMMANDS_AND_TEMPO.md section 15). It carries no timestamp, so
        // two recordings of the demo are the same file byte for byte.
        const auto bank = p.bank();
        if (bank == nullptr) { std::printf("FAIL the record pass has no bank\n"); return 1; }
        songFile = songFileText(*song, *bank, p.bankName());
        if (writeSong != juce::File()) {
            if (!writeSong.replaceWithText(songFile)) { std::printf("FAIL cannot write %s\n", writeSong.getFullPathName().toRawUTF8()); return 1; }
            std::printf("wrote %s (%d bytes)\n", writeSong.getFullPathName().toRawUTF8(), int(songFile.getNumBytesAsUTF8()));
        }
        if (checkSong != juce::File()) {
            // The CTest check: the file in Demo/ is this recording.
            if (!checkSong.existsAsFile()) { std::printf("FAIL %s does not exist; write it with --write-song\n", checkSong.getFullPathName().toRawUTF8()); return 1; }
            const juce::String have = checkSong.loadFileAsString();
            if (have != songFile) {
                const juce::File wrote = outDir.getChildFile("recorded_song.cbsong");
                wrote.replaceWithText(songFile);
                std::printf("FAIL %s is not what recording the demo produces\n", checkSong.getFullPathName().toRawUTF8());
                std::printf("     the recording is %d bytes, the file %d; the recording is at %s\n",
                            int(songFile.getNumBytesAsUTF8()), int(have.getNumBytesAsUTF8()), wrote.getFullPathName().toRawUTF8());
                return 1;
            }
            std::printf("PASSED the demo song file matches a fresh recording (%d bytes)\n", int(songFile.getNumBytesAsUTF8()));
            return 0;
        }
        writeCellListing(outDir.getChildFile("recorded_cells.txt"), *song);
        int cells = 0;
        for (int ch = 0; ch < 4; ++ch)
            for (auto slot : song->chain[size_t(ch)])
                if (const auto* phrase = song->phrase(slot))
                    for (const auto& c : phrase->cells)
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

    /* ---- pass 3: the song file, on the plugin's own transport --------- */
    // Section 16: Demo/ChipBoy Demo.cbsong loaded, the factory bank, no MIDI
    // and no play head at all -- the Standalone's case -- must drive the chip
    // exactly as the recording did.
    Capture ownPass;
    bool ranOwn = false;
    {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        auto& p = *pOwned;
        SongReport report;
        if (!p.loadSongFile(demoSongFile, report)) {
            std::printf("FAIL cannot read the demo song %s\n", demoSongFile.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf("demo song: bank \"%s\", %d instrument slots used, %d differ here\n",
                    report.bankName.toRawUTF8(), report.instrumentsUsed, report.differences.size());
        for (const auto& line : report.differences) std::printf("  %s\n", line.toRawUTF8());
        p.mutateSong([](tracker::Song& s) { for (auto& n : s.noteSource) n = tracker::NoteSource::Tracker; });
        std::vector<driver::RegWrite> log;
        log.reserve(1u << 20);
        RunOptions opt;
        opt.holdAtBarOne = true;
        opt.ownTransport = true;
        run(p, aut, opt, ownPass, log);
        ranOwn = true;
        if (!p.ownsTransport()) { std::printf("FAIL the plugin did not take the transport with no play head\n"); return 1; }
        if (dump) dumpWrites(outDir.getChildFile("writes_own.txt"), ownPass, aut.bpm);
    }

    /* ---- pass 4: MIDI plus the song in Hybrid (section 20) ------------ */
    // The demo song loaded in a tab, all four channels Hybrid, the MIDI file
    // played and nothing automated: the notes come from MIDI, the instruments
    // and the commands from the cells, and the chip must see pass 1 again.
    Capture hybridPass;
    {
        const auto pOwned = std::make_unique<ChipBoyProcessor>();
        auto& p = *pOwned;
        if (!buildHybridProject(p, demoSongFile, aut, /*statics*/ false)) return 1;
        std::printf("hybrid: %d tab(s), the song's own bank, four Hybrid channels\n", p.tabCount());
        std::vector<driver::RegWrite> log;
        log.reserve(1u << 20);
        RunOptions opt;
        opt.midi = &midi;
        opt.noLanes = true;
        opt.skipInertLanes = true;
        run(p, aut, opt, hybridPass, log);
        if (dump) dumpWrites(outDir.getChildFile("writes_hybrid.txt"), hybridPass, aut.bpm);
    }

    /* ---- compare ----------------------------------------------------- */
    const bool ok = compare(recordPass, replayPass, aut.bpm)
                    && (!ranOwn || compare(recordPass, ownPass, aut.bpm))
                    && compare(recordPass, hybridPass, aut.bpm);
    std::printf("\nregister writes per channel\n");
    for (int s = 0; s < kStreams; ++s)
        std::printf("  %-7s record %6d   replay %6d   song file %6d   hybrid %6d\n", kStreamName[s],
                    int(recordPass.streams[size_t(s)].size()), int(replayPass.streams[size_t(s)].size()),
                    int(ownPass.streams[size_t(s)].size()), int(hybridPass.streams[size_t(s)].size()));
    std::printf("output RMS per bar\n");
    for (size_t bar = 0; bar < recordPass.rmsPerBar.size(); ++bar)
        std::printf("  bar %2d   record %.5f   replay %.5f\n", int(bar) + 1,
                    recordPass.rmsPerBar[bar], bar < replayPass.rmsPerBar.size() ? replayPass.rmsPerBar[bar] : 0.0);
    std::printf("\n%s\n", ok ? "PASSED the record test" : "FAILED the record test");
    return ok ? 0 : 1;
}
