// chipboy_linktest -- the two plugins in one process, linked through a
// region file (spec section 16.5). Exit code 0 when every check passes.
#include "core/Bank/Preset.h"
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/BankJson.h"
#include "plugin/shared/Presets.h"
#include "plugin/shared/SongFiles.h"
#include "plugin/voice/VoiceProcessor.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace chipboy::plugin;

namespace {

int failures = 0;
std::FILE* diag = nullptr;          ///< chipboy_linktest.stages.txt in the working directory: what ran, kept through a crash
const char* lastLine = "(before main)";
void line(const char* text)
{
    lastLine = text;
    std::printf("%s\n", text); std::fflush(stdout);   // a crash must not eat the lines before it (CI pipes stdout)
    if (diag) { std::fprintf(diag, "%s\n", text); std::fflush(diag); }
}
void check(bool ok, const char* what)
{
    char buf[256]; std::snprintf(buf, sizeof buf, "%s  %s", ok ? "ok  " : "FAIL", what);
    line(buf);
    if (!ok) ++failures;
}
void stage(const char* what) { char buf[256]; std::snprintf(buf, sizeof buf, "--   %s", what); line(buf); }
void onCrash(int sig)
{
    char buf[320]; std::snprintf(buf, sizeof buf, "CRASH signal %d after: %s", sig, lastLine);
    std::fputs(buf, stderr); std::fputs("\n", stderr); std::fflush(stderr);
    if (diag) { std::fputs(buf, diag); std::fputs("\n", diag); std::fflush(diag); }
    std::_Exit(3);
}

struct FakePlayHead : juce::AudioPlayHead {
    int64_t frame = 0; bool playing = true;
    int numerator = 4, denominator = 4;      ///< the DAW's own signature (section 19)
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setBpm(120.0);
        p.setTimeInSamples(frame);
        p.setPpqPosition(double(frame) / 48000.0 * 2.0);
        p.setTimeSignature(TimeSignature { numerator, denominator });
        return p;
    }
};

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

/// Every ChipBoyProcessor here is built on the heap. One is close to half a
/// megabyte -- the four scope rings and the link region are most of it -- and
/// the six this test needs asked main() for more stack than a Windows
/// executable is given (1 MB by default), so the frame faulted in the
/// prologue, before the first line of the test had run.
std::unique_ptr<ChipBoyProcessor> machine() { return std::make_unique<ChipBoyProcessor>(); }
/// And a whole song is 83 KB, for the same reason.
std::unique_ptr<chipboy::tracker::Song> song() { return std::make_unique<chipboy::tracker::Song>(); }

float peak(const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) p = std::max(p, std::fabs(b.getSample(c, i)));
    return p;
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    diag = std::fopen("chipboy_linktest.stages.txt", "w");
    std::signal(SIGSEGV, onCrash); std::signal(SIGABRT, onCrash); std::signal(SIGILL, onCrash); std::signal(SIGFPE, onCrash);
    stage("juce init");
    juce::ScopedJuceInitialiser_GUI init;
    stage("main processor");
    const auto mainOwned = machine();
    auto& main = *mainOwned;
    stage("voice processor");
    VoiceProcessor voice;
    main.apvts.getParameter(ids::linkMode)->setValueNotifyingHost(1.0f);
    main.prepareToPlay(48000.0, 512);
    voice.prepareToPlay(48000.0, 512);
    FakePlayHead ph;
    main.setPlayHead(&ph); voice.setPlayHead(&ph);

    stage("first pump");
    pump(350);
    check(main.linkActive(), "link mode publishes the instance");
    check(main.getLatencySamples() == main.latencyFrames() + 512, "latency = renderer + one block");

    stage("voice claims PU2");
    voice.setTarget(main.instanceUuid(), 1);   // PU2
    pump(400);
    check(voice.link().status() == LinkClient::Status::Linked, "the Voice claims PU2");
    check((main.voiceOwnedMask() & 2u) != 0, "the main sees the claim");
    check(voice.link().targetName() == main.instanceName(), "the instance name travels");
    check(voice.link().instrumentName(1).isNotEmpty(), "instrument names travel");

    // A second Voice wanting the same channel is refused.
    VoiceProcessor other;
    other.setTarget(main.instanceUuid(), 1);
    pump(400);
    check(other.link().status() == LinkClient::Status::Busy, "a second claim reads channel busy");
    other.setTarget({}, 0);

    juce::AudioBuffer<float> mb(2, 512), vb(2, 512);
    juce::MidiBuffer mm, vm;
    bool pu2Sounded = false, voiceSilent = true; float mainPeak = 0.0f;
    for (int b = 0; b < 60; ++b) {
        ph.frame = int64_t(b) * 512;
        mm.clear(); vm.clear();
        if (b == 2) vm.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8) 100), 10);
        if (b == 40) vm.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
        // alternate the order the host might use
        if (b % 2 == 0) { main.processBlock(mb, mm); voice.processBlock(vb, vm); }
        else            { voice.processBlock(vb, vm); main.processBlock(mb, mm); }
        if (b > 4 && b < 38 && main.channelLevels[1].load() >= 0) pu2Sounded = true;
        if (peak(vb) > 0.0f) voiceSilent = false;
        if (b > 6 && b < 38) mainPeak = std::max(mainPeak, peak(mb));
        pump(2);
    }
    check(pu2Sounded, "a note through the Voice plays PU2 on the main");
    check(mainPeak > 0.01f, "and audio comes out of the main");
    check(voiceSilent, "the Voice's own output is silence");
    check(voice.view().note == 57 || voice.view().active || voice.scopeSnapshot(nullptr, 0) == 0, "the return path carries state");

    // Direct MIDI on a Voice-owned channel is ignored (11.1). MIDI channel 2 -> PU2 by default.
    bool pu2Direct = false;
    for (int b = 60; b < 90; ++b) {
        ph.frame = int64_t(b) * 512;
        mm.clear(); vm.clear();
        if (b == 62) mm.addEvent(juce::MidiMessage::noteOn(2, 60, (juce::uint8) 100), 0);
        main.processBlock(mb, mm); voice.processBlock(vb, vm);
        if (main.channelLevels[1].load() >= 0) pu2Direct = true;
        pump(2);
    }
    check(!pu2Direct, "direct MIDI on the owned channel is ignored");

    // Push to slot lands in the bank; pull brings it back.
    voice.localInstrument().name = "Pushed";
    voice.localInstrument().duty = 3;
    voice.localChanged();
    voice.pushToSlot(50);
    pump(500);
    const auto bank = main.bank();
    check(bank && bank->instruments[49].used && bank->instruments[49].name == "Pushed" && bank->instruments[49].duty == 3, "push to slot writes the bank");
    voice.localInstrument().name = "Scratch";
    voice.pullFromSlot(1);
    pump(500);
    check(voice.localInstrument().name == bank->instruments[0].name, "pull from slot copies the bank's instrument");

    // A Voice letting a channel go stops what it was playing (section 9.1).
    for (int b = 90; b < 100; ++b) {
        ph.frame = int64_t(b) * 512;
        mm.clear(); vm.clear();
        if (b == 92) vm.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        main.processBlock(mb, mm); voice.processBlock(vb, vm);
        pump(2);
    }
    const bool voiceSounding = main.driverView().view(1).active;
    voice.setTarget({}, 0);
    pump(400);
    for (int b = 100; b < 106; ++b) { ph.frame = int64_t(b) * 512; mm.clear(); main.processBlock(mb, mm); pump(2); }
    check(voiceSounding && !main.driverView().view(1).active, "a Voice letting a channel go stops what it was playing");

    // Link off: the Voice sees the instance go, and the block of MIDI held
    // back for it is played rather than dropped (section 9.1).
    ph.frame = 106 * 512; mm.clear();
    mm.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    main.processBlock(mb, mm);
    ph.frame = 107 * 512; mm.clear();
    mm.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 0);
    main.processBlock(mb, mm);
    check(!main.driverView().view(0).active, "link mode holds a block of MIDI back");
    main.apvts.getParameter(ids::linkMode)->setValueNotifyingHost(0.0f);
    pump(400);
    check(!main.linkActive(), "link mode off unpublishes");
    check(voice.link().status() != LinkClient::Status::Linked, "the Voice is no longer linked");
    ph.frame = 108 * 512; mm.clear();
    main.processBlock(mb, mm);
    check(main.driverView().view(0).active, "the MIDI it was holding plays when link mode goes");

    /* ---- the song file carries the grooves, the VEL column and the steps
       (docs/COMMANDS_AND_TEMPO.md sections 9.1 and 9.2) ---------------- */
    {
        const auto aOwned = machine();
        auto& a = *aOwned;
        a.mutateSong([](chipboy::tracker::Song& s) {
            s.grooves[2].ticks = { 4, 5, 6, 7 };
            s.phrases[0].used = true;
            s.phrases[0].groove = 16;
            s.phrases[0].cells[0].note = 60;
            s.phrases[0].cells[0].vel = 42;
            // A cell command's revert form is its `c` field (section 3).
            s.phrases[0].cells[0].cmd1 = chipboy::bank::revertOf(chipboy::bank::Cmd::E);
            s.phrases[0].cells[0].cmd2 = { chipboy::bank::Cmd::V, 4, 6, 0 };
            s.phrases[0].steps = 8;                   // the phrase's own length (section 25)
        });
        juce::MemoryBlock state;
        a.getStateInformation(state);
        const auto bOwned = machine();
        auto& b = *bOwned;
        b.setStateInformation(state.getData(), int(state.getSize()));
        const auto s = b.song();
        check(s && s->grooves[2].length() == 4 && s->grooves[2].at(3) == 7, "a groove's sixteen ticks round-trip through the song");
        check(s && s->phrases[0].groove == 16, "a phrase's groove slot 16 round-trips");
        check(s && s->phrases[0].cells[0].vel == 42, "the VEL column round-trips");
        check(s && s->phrases[0].steps == 8, "a phrase's own length round-trips");
        check(s && s->phrases[0].cells[0].cmd1.cmd == chipboy::bank::Cmd::E && chipboy::bank::isRevert(s->phrases[0].cells[0].cmd1),
              "a cell command's revert form round-trips");
        check(s && !chipboy::bank::isRevert(s->phrases[0].cells[0].cmd2) && s->phrases[0].cells[0].cmd2.a == 4,
              "a cell command with a value does not come back as a revert");
        const auto oldOwned = song();
        auto& old = *oldOwned;
        const bool read = songFromJson("{\"format\":\"chipboy-song\",\"grooves\":[[7,5]]}", old);
        check(read && old.grooves[0].length() == 2 && old.grooves[0].at(0) == 7 && old.grooves[0].at(1) == 5, "the old two-entry groove form still reads");
        // A file written before the field existed reads as a plain command.
        const auto noCOwned = song();
        auto& noC = *noCOwned;
        const bool readOld = songFromJson("{\"format\":\"chipboy-song\",\"phrases\":[{\"slot\":1,\"steps\":[{\"c1\":{\"c\":\"E\",\"a\":9,\"b\":3}}]}]}", noC);
        check(readOld && noC.phrases[0].cells[0].cmd1.c == 0 && noC.phrases[0].cells[0].cmd1.a == 9,
              "a cell command written before the revert field reads as a value");
    }

    /* ---- the Song tempo parameter is the song's base tempo (section 4) - */
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        // A published song with no T cells: the tracker runs at the parameter.
        p.mutateSong([](chipboy::tracker::Song& s) { s.noteSource[0] = chipboy::tracker::NoteSource::Tracker; });
        p.apvts.getParameter(ids::tempoSource)->setValueNotifyingHost(1.0f);              // Song
        auto* tempo = p.apvts.getParameter(ids::songTempo);
        tempo->setValueNotifyingHost(tempo->getNormalisableRange().convertTo0to1(150.0f));
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer empty;
        for (int b = 0; b < 94; ++b) { head.frame = int64_t(b) * 512; p.processBlock(ab, empty); }
        const int64_t at1s = p.trackerTick();
        check(std::abs(double(p.effectiveTempo()) - 150.0) < 0.5, "a published song runs at the Song tempo parameter");
        check(at1s > 56 && at1s < 62, "150 BPM is 60 ticks a second, not the song's stored 120");
        // A song saved from here carries that tempo, and one loaded brings its own back.
        juce::MemoryBlock state;
        p.getStateInformation(state);
        const auto writtenOwned = song();
        auto& written = *writtenOwned;
        const auto tree = juce::ValueTree::readFromData(state.getData(), state.getSize());
        const auto saved = tree.getChildWithName("tabs").getChild(0);
        check(saved.isValid() && songFromJson(saved["song"].toString(), written) && std::abs(written.tempoBpm - 150.0) < 1e-6,
              "the saved song carries the Song tempo parameter's value");
        const auto qOwned = machine();
        auto& q = *qOwned;
        q.setStateInformation(state.getData(), int(state.getSize()));
        const auto reopened = q.song();
        check(reopened && std::abs(reopened->tempoBpm - 150.0) < 1e-6, "a saved song opens at its own tempo");
        check(std::abs(double(paramInt(q.apvts.getRawParameterValue(ids::songTempo))) - 150.0) < 0.5,
              "and its tempo is the Song tempo parameter's value");
    }

    /* ---- a channel whose feed changes hands is flushed (section 9.1) -- */
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer mb2;
        auto blocks = [&](int from, int to, int noteAt = -1, int note = 60) {
            for (int b = from; b < to; ++b) {
                head.frame = int64_t(b) * 512;
                mb2.clear();
                if (b == noteAt) mb2.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
                p.processBlock(ab, mb2);
                pump(1);
            }
        };
        blocks(0, 8, 1);
        const bool sounding = p.driverView().view(0).active;
        p.apvts.getParameter(channelParamId(0, ids::source))->setValueNotifyingHost(1.0f);   // Off
        blocks(8, 12);
        check(sounding && !p.driverView().view(0).active, "changing a channel's Source silences it");

        // The same when the channel goes over to the tracker's own notes.
        p.apvts.getParameter(channelParamId(0, ids::source))->setValueNotifyingHost(0.0f);   // Omni
        blocks(12, 20, 13);
        const bool again = p.driverView().view(0).active;
        p.mutateSong([](chipboy::tracker::Song& s) { s.noteSource[0] = chipboy::tracker::NoteSource::Tracker; });
        blocks(20, 24);
        check(again && !p.driverView().view(0).active, "a channel changing to Trk drops the note the piano roll left");
    }

    /* ---- the position stands still while the transport does (9.1) ---- */
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer empty;
        for (int b = 0; b < 20; ++b) { head.frame = int64_t(b) * 512; p.processBlock(ab, empty); }
        const int64_t at = p.trackerTick();
        head.playing = false;                       // stopped, and the host stays put
        for (int b = 0; b < 40; ++b) p.processBlock(ab, empty);
        check(at > 0 && p.trackerTick() == at, "the tracker position stands still while the transport is stopped");
    }

    /* ---- recording: keyswitches are not cells, disarming flushes ------ */
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        p.mutateSong([](chipboy::tracker::Song& s) { s.noteSource[0] = chipboy::tracker::NoteSource::Tracker; });
        p.apvts.getParameter(channelParamId(0, ids::keyswitch))->setValueNotifyingHost(1.0f);
        p.setRecordArm(true);
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer mb2;
        auto play = [&](int from, int to, int onAt, int note, int offAt) {
            for (int b = from; b < to; ++b) {
                head.frame = int64_t(b) * 512;
                mb2.clear();
                if (b == onAt) mb2.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
                if (b == offAt) mb2.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                p.processBlock(ab, mb2);
                pump(1);
            }
            pump(150);
        };
        play(0, 24, 2, 27, 8);            // the pulse keyswitch octave is 24-35
        auto s = p.song();
        check(s && s->chain[0].empty(), "a keyswitch note is never recorded as a cell");
        play(24, 48, 26, 60, 34);
        s = p.song();
        bool wrote = false;
        if (s) for (const auto& phrase : s->phrases) if (phrase.used) for (const auto& c : phrase.cells) if (c.note == 60) wrote = true;
        check(wrote, "a played note is recorded as a cell");
        play(48, 60, 50, 62, -1);         // still held
        const bool sounding = p.driverView().view(0).active;
        p.setRecordArm(false);
        play(60, 66, -1, 0, -1);
        check(sounding && !p.driverView().view(0).active, "disarming record stops what was playing through");
    }

    /* ---- phrase lengths, and a song file that says what bank it wants
       (docs/COMMANDS_AND_TEMPO.md sections 25 and 15) ------------------ */
    stage("song files");
    {
        const auto aOwned = machine();
        auto& a = *aOwned;
        a.mutateSong([](chipboy::tracker::Song& s) {
            s.recordArm = { true, false, true, false };
            s.phrases[0].used = true;
            s.phrases[0].steps = 24;                  // the phrase's own length
            s.phrases[1].used = true;
            s.phrases[1].steps = 8;                   // and another channel's, shorter
            s.phrases[0].cells[40].note = 64;         // a cell past the old sixteen
            s.phrases[0].cells[40].inst = 3;
            s.phrases[0].cells[63].cmd1 = { chipboy::bank::Cmd::V, 9, 4, 0 };
            s.chain[0] = { 1, 1, 1 };
            s.chain[1] = { 2, 2, 2 };
            s.noteSource[0] = chipboy::tracker::NoteSource::Tracker;
        });
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile("chipboy_linktest.cbsong");
        check(a.saveSongFile(file) && file.existsAsFile(), "a song file is written");
        // The bank it was written with is named, and so is every instrument
        // slot the song uses -- a load says where this bank differs.
        const auto bOwned = machine();
        auto& b = *bOwned;
        b.mutateBank([](chipboy::bank::Bank& into) { into.instruments[2].name = "Something else"; });
        SongReport report;
        // Format 5: the file brings the bank it was written with, so it plays
        // through that one and this bank's renaming is beside the point (18).
        const bool loaded = b.loadSongFile(file, report);
        const auto s = b.song();
        check(loaded && s != nullptr, "and read back");
        check(s && s->phrases[0].steps == 24 && s->phrases[1].steps == 8, "a phrase's length is a number 1-64 and round-trips");
        check(s && s->stepsOfRow(0, 0) == 24 && s->stepsOfRow(1, 0) == 8, "two channels play rows of different lengths");
        check(s && s->phrases[0].cells[40].note == 64 && s->phrases[0].cells[40].inst == 3, "a cell at step 40 round-trips");
        check(s && s->phrases[0].cells[63].cmd1.cmd == chipboy::bank::Cmd::V, "and so does one at step 63");
        check(s && !s->recordArm[1] && !s->recordArm[3] && s->recordArm[0], "the record arms round-trip");
        // 24 steps of six ticks against 8: the two channels drift by design.
        check(s && chipboy::tracker::rowStartTick(*s, 0, 2) == 2 * 144 && chipboy::tracker::rowStartTick(*s, 1, 2) == 2 * 48,
              "the row tables are built per channel when the song is published");
        check(s && chipboy::tracker::songTicks(*s) == 3 * 144 && chipboy::tracker::longestChain(*s) == 0,
              "and the song is as long as its longest channel");
        check(report.bankName == "Factory", "the song file names the bank it was written with");
        check(report.hasBank && report.instrumentsUsed == 1 && report.differences.isEmpty(),
              "and carries it, so nothing differs");
        const auto loadedBank = b.bank();
        check(loadedBank && juce::String(loadedBank->instruments[2].name) != "Something else",
              "loading a format-5 song replaces this tab's bank with the file's");
        file.deleteFile();

        // A song written before format 4: sixteen dense cells, steps 8 or 16.
        // Section 25 gives every used phrase the file's steps per bar.
        const auto oldOwned = song();
        auto& old = *oldOwned;
        const bool readOld = songFromJson("{\"format\":\"chipboy-song\",\"stepsPerBar\":8,"
                                          "\"phrases\":[{\"slot\":1,\"steps\":[{},{\"n\":62},{},{\"n\":64}]}],"
                                          "\"chains\":[[1]]}", old);
        check(readOld && old.phrases[0].steps == 8 && old.phrases[0].cells[1].note == 62 && old.phrases[0].cells[3].note == 64,
              "a song written before format 4 still reads, at the file's steps per bar");
        check(readOld && old.recordArm[0] && old.recordArm[3], "and its channels are all armed");

        // A format-5 bar override becomes the length of the phrase in that
        // bar; a phrase used under two different overrides is duplicated.
        const auto fiveOwned = song();
        auto& five = *fiveOwned;
        const bool read5 = songFromJson("{\"format\":\"chipboy-song\",\"version\":5,\"steps\":16,"
                                        "\"barSteps\":[0,12],"
                                        "\"phrases\":[{\"slot\":1,\"steps\":[{\"s\":0,\"n\":60}]}],"
                                        "\"chains\":[[1,1],[1]]}", five);
        check(read5 && five.phrases[0].steps == 16, "a format-5 phrase takes the file's steps per bar");
        const int dup = five.chain[0].size() > 1 ? five.chain[0][1] : 0;
        check(read5 && dup > 1 && five.phrases[size_t(dup - 1)].used && five.phrases[size_t(dup - 1)].steps == 12,
              "and a bar override becomes the length of a copy of it");
        check(read5 && five.phrases[size_t(dup - 1)].cells[0].note == 60 && five.chain[1][0] == 1,
              "the copy holds the same cells, and the bar without the override keeps the original");
    }

    /* ---- the record arms (section 14) -------------------------------- */
    stage("record arms");
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        // Every channel listens to every MIDI channel here, so one note
        // reaches all four: what differs is the arm and the playback source.
        for (int ch = 1; ch < 4; ++ch)
            p.apvts.getParameter(channelParamId(ch, ids::source))->setValueNotifyingHost(0.0f);   // Omni
        p.mutateSong([](chipboy::tracker::Song& s) {
            s.noteSource[0] = chipboy::tracker::NoteSource::Tracker;   // armed: it records and sounds
            s.noteSource[1] = chipboy::tracker::NoteSource::Tracker;   // unarmed: neither
            s.noteSource[2] = chipboy::tracker::NoteSource::PianoRoll; // armed, and playing from MIDI
            s.recordArm = { true, false, true, false };
        });
        p.setRecordArm(true);
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer mb2;
        bool armedSounded = false, unarmedSounded = false;
        for (int b = 0; b < 40; ++b) {
            head.frame = int64_t(b) * 512;
            mb2.clear();
            if (b == 2) mb2.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            if (b == 20) mb2.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            p.processBlock(ab, mb2);
            if (b > 3 && b < 19) {
                if (p.driverView().view(0).active) armedSounded = true;
                if (p.driverView().view(1).active) unarmedSounded = true;
            }
            pump(1);
        }
        p.setRecordArm(false);
        pump(200);
        const auto s = p.song();
        auto wrote = [&s](int ch) {
            if (!s) return false;
            for (auto slotN : s->chain[size_t(ch)])
                if (const auto* phrase = s->phrase(slotN))
                    for (const auto& c : phrase->cells) if (c.note == 60) return true;
            return false;
        };
        check(wrote(0), "an armed channel records what it is played");
        check(!wrote(1), "an unarmed channel never records");
        check(wrote(2), "an armed channel records whatever its playback source is");
        check(!wrote(3), "and an unarmed one does not, either way");
        check(armedSounded, "an armed tracker channel lets the MIDI through while recording");
        check(!unarmedSounded, "an unarmed tracker channel drops it");
    }

    /* ---- the command octave records as a slot-only cell (13) --------- */
    stage("the command octave");
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        p.mutateSong([](chipboy::tracker::Song& s) { s.noteSource[0] = chipboy::tracker::NoteSource::Tracker; });
        auto* slot = p.apvts.getParameter(channelParamId(0, ids::cmd1Type));
        slot->setValueNotifyingHost(slot->getNormalisableRange().convertTo0to1(float(choiceFromCmd(chipboy::bank::Cmd::V))));
        auto* x = p.apvts.getParameter(channelParamId(0, ids::cmd1X));
        x->setValueNotifyingHost(x->getNormalisableRange().convertTo0to1(9.0f));
        auto* y = p.apvts.getParameter(channelParamId(0, ids::cmd1Y));
        y->setValueNotifyingHost(y->getNormalisableRange().convertTo0to1(4.0f));
        p.setRecordArm(true);
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer mb2;
        int heldPeriod = 0;
        for (int b = 0; b < 40; ++b) {
            head.frame = int64_t(b) * 512;
            mb2.clear();
            if (b == 2) mb2.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            if (b == 12) mb2.addEvent(juce::MidiMessage::noteOn(1, 0, (juce::uint8) 100), 0);   // the command octave
            if (b == 20) mb2.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            p.processBlock(ab, mb2);
            if (b == 11) heldPeriod = p.driverView().view(0).note;
            pump(1);
        }
        p.setRecordArm(false);
        pump(200);
        const auto s = p.song();
        bool sawNoteZero = false, sawSlotCell = false;
        if (s) for (auto slotN : s->chain[0])
            if (const auto* phrase = s->phrase(slotN))
                for (const auto& c : phrase->cells) {
                    if (c.note == 0 && c.cmd1.cmd == chipboy::bank::Cmd::V && c.cmd1.a == 9) sawSlotCell = true;
                    if (c.note >= 1 && c.note < 12) sawNoteZero = true;
                }
        check(heldPeriod == 60, "a note in the command octave leaves the held note alone");
        check(sawSlotCell, "and records as a slot-only cell");
        check(!sawNoteZero, "the command octave itself is never a note in a cell");
    }

    /* ---- the tracker's own transport (section 16) -------------------- */
    stage("the tracker's own transport");
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        p.setPlayHead(nullptr);                        // the Standalone's case
        p.mutateSong([](chipboy::tracker::Song& s) {
            s.noteSource[0] = chipboy::tracker::NoteSource::Tracker;
            s.phrases[0].used = true;
            s.phrases[0].cells[0].note = 60;
            s.phrases[0].cells[0].inst = 1;
            s.phrases[0].cells[8].note = 67;
            s.phrases[0].cells[8].inst = 1;
            s.chain[0] = { 1, 1 };
        });
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer empty;
        for (int b = 0; b < 8; ++b) p.processBlock(ab, empty);
        check(p.ownsTransport(), "with no play head the plugin owns the transport");
        check(!p.transportPlaying(), "and it starts stopped");
        check(p.driverView().view(0).note == 0, "so no cell has played");
        p.transportPlay();
        float loudest = 0.0f;
        int notes = 0, last = -1;
        for (int b = 0; b < 200; ++b) {
            p.processBlock(ab, empty);
            const int n = p.driverView().view(0).note;
            if (n != last && n != 0) { ++notes; last = n; }
            loudest = std::max(loudest, peak(ab));
        }
        check(p.transportPlaying(), "Play runs it");
        check(notes >= 2, "the song's cells play on the plugin's own clock");
        check(loudest > 0.01f, "and audio comes out");
        check(p.trackerTick() > 90, "the position follows the song's tempo");
        // It loops: two bars, and the position comes back round.
        p.setLoop(true);
        p.setLoopRows(0, 2);
        int64_t highest = 0;
        bool wrapped = false;
        for (int b = 0; b < 600; ++b) {
            p.processBlock(ab, empty);
            const int64_t at = p.trackerTick();
            if (at + 8 < highest) wrapped = true;
            highest = std::max(highest, at);
        }
        check(wrapped, "and the loop comes back round");
        check(highest < 200, "without running past the loop's end");
        p.transportStop();
        for (int b = 0; b < 4; ++b) p.processBlock(ab, empty);
        check(!p.transportPlaying(), "Stop stops it");
        check(!p.driverView().view(0).active, "and nothing is left ringing");
    }

    /* ---- song tabs (section 18) -------------------------------------- */
    stage("song tabs");
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        check(p.tabCount() == 1 && p.activeTab() == 0, "a fresh plugin has one tab");
        p.editSong("first", [](chipboy::tracker::Song& s) { s.phrases[0].used = true; s.phrases[0].cells[0].note = 61; s.phrases[0].steps = 12; });
        p.editBank("first bank", [](chipboy::bank::Bank& b) { b.instruments[0].name = "Tab one lead"; });
        p.setBankNameEdit("One");
        const int second = p.newTab();
        check(second == 1 && p.tabCount() == 2 && p.activeTab() == 1, "the + tab opens a new song");
        check(p.song() && p.song()->phrases[0].cells[0].note == 0 && p.song()->phrases[0].length() == 16, "which is empty");
        check(p.bank() && p.bankName() == "Factory" && juce::String(p.bank()->instruments[0].name) != "Tab one lead",
              "and starts with the factory bank");
        p.editSong("second", [](chipboy::tracker::Song& s) { s.phrases[0].used = true; s.phrases[0].cells[0].note = 72; });
        p.setActiveTab(0);
        check(p.song() && p.song()->phrases[0].cells[0].note == 61 && p.song()->phrases[0].length() == 12, "switching back brings the first song");
        check(p.bank() && juce::String(p.bank()->instruments[0].name) == "Tab one lead" && p.bankName() == "One",
              "with its own bank and bank name");
        p.setActiveTab(1);
        check(p.song() && p.song()->phrases[0].cells[0].note == 72, "and forward again brings the second");

        // An undo step belongs to its tab and re-activates it.
        p.setActiveTab(1);
        check(p.history().canUndo(), "the history holds the edits");
        while (p.history().canUndo()) p.history().undo();
        check(p.activeTab() == 0 && p.song() && p.song()->phrases[0].cells[0].note == 0,
              "undoing the first tab's edit goes back to that tab");
        while (p.history().canRedo()) p.history().redo();
        check(p.tabCount() == 2 && p.song() && p.song()->phrases[0].cells[0].note == 72, "and redo lands on the last one again");

        // Two tabs through the plugin state.
        juce::MemoryBlock state;
        p.getStateInformation(state);
        const auto qOwned = machine();
        auto& q = *qOwned;
        q.setStateInformation(state.getData(), int(state.getSize()));
        check(q.tabCount() == 2 && q.activeTab() == 1, "both tabs and the active one round-trip through the state");
        check(q.song() && q.song()->phrases[0].cells[0].note == 72, "the active tab is the one that was saved");
        q.setActiveTab(0);
        check(q.song() && q.song()->phrases[0].cells[0].note == 61 && q.song()->phrases[0].length() == 12, "and the other tab came with it");
        check(q.bank() && juce::String(q.bank()->instruments[0].name) == "Tab one lead" && q.bankName() == "One",
              "each tab keeps its own bank");
        check(q.closeTab(0) && q.tabCount() == 1, "a tab closes");
        check(q.song() && q.song()->phrases[0].cells[0].note == 72, "and what is left plays");
        check(!q.closeTab(0), "the last tab does not");

        // A state written before tabs is one tab.
        juce::ValueTree old("ChipBoyState");
        old.setProperty("version", 1, nullptr);
        old.setProperty("bankName", "Old", nullptr);
        old.setProperty("song", songToJson(*q.song()), nullptr);
        old.setProperty("bank", bankToJson(*q.bank()), nullptr);
        juce::MemoryBlock oldState;
        { juce::MemoryOutputStream mo(oldState, false); old.writeToStream(mo); }
        const auto rOwned = machine();
        auto& r = *rOwned;
        r.setStateInformation(oldState.getData(), int(oldState.getSize()));
        check(r.tabCount() == 1 && r.bankName() == "Old" && r.song() && r.song()->phrases[0].cells[0].note == 72,
              "a state written before tabs loads as one tab");
    }

    /* ---- song file format 5 carries the bank (section 18) ------------- */
    stage("song files, format 5");
    {
        const auto aOwned = machine();
        auto& a = *aOwned;
        a.editBank("bank", [](chipboy::bank::Bank& b) { b.instruments[6].name = "Song's own wave"; b.instruments[6].used = true; });
        a.setBankNameEdit("Travelling");
        a.editSong("song", [](chipboy::tracker::Song& s) {
            s.phrases[0].used = true; s.phrases[0].cells[0].note = 64; s.phrases[0].cells[0].inst = 7;
            s.chain[0] = { 1 };
            s.noteSource[0] = chipboy::tracker::NoteSource::Hybrid;    // and the third source
            s.noteSource[2] = chipboy::tracker::NoteSource::Tracker;
        });
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile("chipboy_linktest_f5.cbsong");
        check(a.saveSongFile(file) && file.existsAsFile(), "a format-5 song file is written");
        check(!a.tabDirty(a.activeTab()) && a.tabFile(a.activeTab()) == file, "and the tab is saved and named after it");

        const auto bOwned = machine();
        auto& b = *bOwned;
        SongReport report;
        check(b.openSongFileInTab(file, report) && b.tabCount() == 2 && b.activeTab() == 1, "opening one opens a tab");
        check(report.hasBank && report.differences.isEmpty(), "which brings its own bank, so nothing differs");
        check(b.bank() && juce::String(b.bank()->instruments[6].name) == "Song's own wave", "the sounds travel with the song");
        check(b.bankName() == "Travelling", "and so does the bank's name");
        check(b.song() && b.song()->noteSource[0] == chipboy::tracker::NoteSource::Hybrid
              && b.song()->noteSource[2] == chipboy::tracker::NoteSource::Tracker, "Hybrid round-trips through the song file");
        check(b.tabFile(1) == file && !b.tabDirty(1), "the tab remembers the file it came from");
        {
            // ... and through the plugin state, with the file it came from.
            juce::MemoryBlock state;
            b.getStateInformation(state);
            const auto dOwned = machine();
            auto& d = *dOwned;
            d.setStateInformation(state.getData(), int(state.getSize()));
            check(d.tabCount() == 2 && d.activeTab() == 1 && d.song()
                  && d.song()->noteSource[0] == chipboy::tracker::NoteSource::Hybrid,
                  "Hybrid round-trips through the plugin state");
            check(d.tabFile(1) == file && d.tabName(1) == file.getFileNameWithoutExtension(),
                  "and so does the tab's file and name");
        }
        b.setActiveTab(0);
        check(b.bank() && juce::String(b.bank()->instruments[6].name) != "Song's own wave", "the other tab keeps its own bank");

        // A format-4 file -- the song alone -- takes a copy of the active bank.
        const auto oldOwned = song();
        auto& old = *oldOwned;
        old.phrases[0].used = true; old.phrases[0].cells[0].note = 55; old.phrases[0].cells[0].inst = 7;
        old.chain[0] = { 1 };
        const juce::File f4 = juce::File::getCurrentWorkingDirectory().getChildFile("chipboy_linktest_f4.cbsong");
        // The wrapper without the bank object: what every file written before
        // format 5 looks like.
        f4.replaceWithText("{\"format\":\"chipboy-song-file\",\"version\":1,\"bank\":\"Older\","
                           "\"instruments\":{\"7\":\"Was called this\"},\"song\":" + songToJson(old) + "}");
        const auto cOwned = machine();
        auto& c = *cOwned;
        c.editBank("bank", [](chipboy::bank::Bank& bk) { bk.instruments[6].name = "This bank's"; bk.instruments[6].used = true; });
        SongReport old4;
        check(c.openSongFileInTab(f4, old4) && c.tabCount() == 2, "a format-4 file opens a tab too");
        check(!old4.hasBank && old4.differences.size() == 1 && old4.differences[0].contains("Was called this"),
              "with a copy of the active bank, and it says where that differs");
        check(c.bank() && juce::String(c.bank()->instruments[6].name) == "This bank's", "the copy is this bank");
        check(c.song() && c.song()->phrases[0].cells[0].note == 55, "and the song is the file's");
        file.deleteFile();
        f4.deleteFile();
    }

    /* ---- the master tempo mirrors the parameter both ways (19) -------- */
    stage("master tempo");
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        p.setMasterTempo(160.0);
        check(p.song() && std::abs(p.song()->tempoBpm - 160.0) < 1e-6, "the master tempo goes into the song");
        check(std::abs(double(paramInt(p.apvts.getRawParameterValue(ids::songTempo))) - 160.0) < 0.5,
              "and into the Song tempo parameter");
        check(p.history().canUndo(), "it is one undo step");
        p.history().undo();
        check(p.song() && std::abs(p.song()->tempoBpm - 120.0) < 1e-6
              && std::abs(double(paramInt(p.apvts.getRawParameterValue(ids::songTempo))) - 120.0) < 0.5,
              "undoing puts both back");
        p.history().redo();
        // A host moving the lane writes back into the song, with no undo step.
        auto* tempo = p.apvts.getParameter(ids::songTempo);
        tempo->setValueNotifyingHost(tempo->getNormalisableRange().convertTo0to1(96.0f));
        pump(300);                                     // the write-back is on the timer
        check(p.song() && std::abs(p.song()->tempoBpm - 96.0) < 1e-6, "automation writes back into the song");
        // A second tab has its own, and activating one hands it to the parameter.
        p.newTab();
        p.setMasterTempo(200.0);
        p.setActiveTab(0);
        check(p.song() && std::abs(p.song()->tempoBpm - 96.0) < 1e-6
              && std::abs(double(paramInt(p.apvts.getRawParameterValue(ids::songTempo))) - 96.0) < 0.5,
              "each tab has its own master tempo, and the parameter follows the active one");
        p.setActiveTab(1);
        check(std::abs(double(paramInt(p.apvts.getRawParameterValue(ids::songTempo))) - 200.0) < 0.5,
              "and back");
        // The header's readout: the host's BPM in Host mode, the song's in Song.
        FakePlayHead head;
        p.setPlayHead(&head);
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer empty;
        for (int b = 0; b < 4; ++b) { head.frame = int64_t(b) * 512; p.processBlock(ab, empty); }
        check(std::abs(p.effectiveTempo() - 120.0) < 0.5, "in Host mode the readout is the host's tempo");
        p.apvts.getParameter(ids::tempoSource)->setValueNotifyingHost(1.0f);
        for (int b = 4; b < 8; ++b) { head.frame = int64_t(b) * 512; p.processBlock(ab, empty); }
        check(std::abs(p.effectiveTempo() - 200.0) < 0.5, "in Song mode it is the song's tempo in force");
        p.setPlayHead(nullptr);
    }

    /* ---- the host's time signature stays out (sections 11, 19) -------- */
    stage("host signature");
    {
        const auto pOwned = machine();
        auto& p = *pOwned;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead head;
        p.setPlayHead(&head);
        p.mutateSong([](chipboy::tracker::Song& s) {
            s.phrases[0].used = true;
            for (int i = 0; i < 16; ++i) s.phrases[0].cells[size_t(i)].note = uint8_t(60 + i);
            s.chain[0] = { 1, 1, 1, 1, 1, 1, 1, 1 };
            s.noteSource[0] = chipboy::tracker::NoteSource::Tracker;
        });
        const auto s = p.song();
        check(s && chipboy::tracker::rowStartTick(*s, 0, 3) == 3 * 96, "a row of sixteen straight steps is 96 ticks, so row 3 starts at 3 x 96");
        juce::AudioBuffer<float> ab(2, 512);
        juce::MidiBuffer empty;
        int64_t last = -1;
        bool continuous = true, steady = true;
        for (int b = 0; b < 200; ++b) {
            // The DAW changes its own signature under a 4/4 song.
            if (b == 60) { head.numerator = 3; head.denominator = 4; }
            if (b == 120) { head.numerator = 5; head.denominator = 4; }
            head.frame = int64_t(b) * 512;
            p.processBlock(ab, empty);
            const int64_t at = p.trackerTick();
            if (last >= 0 && (at < last || at > last + 40)) continuous = false;
            last = at;
            if (p.song() && chipboy::tracker::rowStartTick(*p.song(), 0, 1) != 96) steady = false;
        }
        check(steady, "a host signature change does not move the song's rows");
        check(continuous, "and the ticks run on across it");
        check(p.channelRow(0) == int(last / 96), "so the position is still the channel's own row");
    }

    /* ---- an instrument preset saves, loads and is placed (15) -------- */
    stage("instrument presets");
    {
        const auto srcOwned = machine();
        auto& src = *srcOwned;
        src.mutateBank([](chipboy::bank::Bank& into) {
            into.instruments[39] = chipboy::bank::Instrument::defaults(chipboy::bank::InstrumentType::Wave, "Preset lead");
            into.instruments[39].used = true;
            into.instruments[39].table = 20;
            into.instruments[39].wave = 30;
            into.tables[19].used = true; into.tables[19].name = "preset table";
            into.tables[19].steps[0].cmd1 = { chipboy::bank::Cmd::A, 21, 0, 0 };
            into.tables[20].used = true; into.tables[20].name = "chained table";
            into.waves[29].used = true; into.waves[29].name = "preset wave";
            into.waves[29].frames.assign(1, chipboy::bank::frameSaw());
        });
        const auto srcBank = src.bank();
        const auto preset = chipboy::bank::collectPreset(*srcBank, 40);
        check(preset.tables.size() == 2 && preset.waves.size() == 1, "a preset collects the table it chains and its wave");
        const juce::String text = presetToJson(preset);
        chipboy::bank::Preset back;
        check(presetFromJson(text, back), "a preset writes and reads as JSON");
        check(back.instrument.name == "Preset lead" && back.tables.size() == 2 && back.waves.size() == 1
              && back.tables[0].first == 20 && back.waves[0].first == 30, "and comes back with its slots");
        const auto emptyBank = std::unique_ptr<chipboy::bank::Bank>(new chipboy::bank::Bank(chipboy::bank::Bank::empty()));
        chipboy::bank::PlaceReport placed;
        const bool ok = chipboy::bank::placePreset(*emptyBank, back, 5, placed);
        check(ok && emptyBank->instruments[4].used && emptyBank->instruments[4].table == 1
              && emptyBank->instruments[4].wave == 1, "placing it renumbers what it references");
        check(emptyBank->tables[0].steps[0].cmd1.a == 2, "and the table's own A follows its table");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
