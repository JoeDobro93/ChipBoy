// chipboy_linktest -- the two plugins in one process, linked through a
// region file (spec section 16.5). Exit code 0 when every check passes.
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/BankJson.h"
#include "plugin/voice/VoiceProcessor.h"

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
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setBpm(120.0);
        p.setTimeInSamples(frame);
        p.setPpqPosition(double(frame) / 48000.0 * 2.0);
        p.setTimeSignature(TimeSignature { 4, 4 });
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
            s.phrases[0].steps[0].note = 60;
            s.phrases[0].steps[0].vel = 42;
            // A cell command's revert form is its `c` field (section 3).
            s.phrases[0].steps[0].cmd1 = chipboy::bank::revertOf(chipboy::bank::Cmd::E);
            s.phrases[0].steps[0].cmd2 = { chipboy::bank::Cmd::V, 4, 6, 0 };
            s.stepsPerBar = 8;
        });
        juce::MemoryBlock state;
        a.getStateInformation(state);
        const auto bOwned = machine();
        auto& b = *bOwned;
        b.setStateInformation(state.getData(), int(state.getSize()));
        const auto s = b.song();
        check(s && s->grooves[2].length() == 4 && s->grooves[2].at(3) == 7, "a groove's sixteen ticks round-trip through the song");
        check(s && s->phrases[0].groove == 16, "a phrase's groove slot 16 round-trips");
        check(s && s->phrases[0].steps[0].vel == 42, "the VEL column round-trips");
        check(s && s->stepsPerBar == 8, "steps per bar round-trips");
        check(s && s->phrases[0].steps[0].cmd1.cmd == chipboy::bank::Cmd::E && chipboy::bank::isRevert(s->phrases[0].steps[0].cmd1),
              "a cell command's revert form round-trips");
        check(s && !chipboy::bank::isRevert(s->phrases[0].steps[0].cmd2) && s->phrases[0].steps[0].cmd2.a == 4,
              "a cell command with a value does not come back as a revert");
        const auto oldOwned = song();
        auto& old = *oldOwned;
        const bool read = songFromJson("{\"format\":\"chipboy-song\",\"grooves\":[[7,5]]}", old);
        check(read && old.grooves[0].length() == 2 && old.grooves[0].at(0) == 7 && old.grooves[0].at(1) == 5, "the old two-entry groove form still reads");
        // A file written before the field existed reads as a plain command.
        const auto noCOwned = song();
        auto& noC = *noCOwned;
        const bool readOld = songFromJson("{\"format\":\"chipboy-song\",\"phrases\":[{\"slot\":1,\"steps\":[{\"c1\":{\"c\":\"E\",\"a\":9,\"b\":3}}]}]}", noC);
        check(readOld && noC.phrases[0].steps[0].cmd1.c == 0 && noC.phrases[0].steps[0].cmd1.a == 9,
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
        check(std::abs(double(p.tempoInForce()) - 150.0) < 0.5, "a published song runs at the Song tempo parameter");
        check(at1s > 56 && at1s < 62, "150 BPM is 60 ticks a second, not the song's stored 120");
        // A song saved from here carries that tempo, and one loaded brings its own back.
        juce::MemoryBlock state;
        p.getStateInformation(state);
        const auto writtenOwned = song();
        auto& written = *writtenOwned;
        const auto tree = juce::ValueTree::readFromData(state.getData(), state.getSize());
        check(tree.isValid() && songFromJson(tree["song"].toString(), written) && std::abs(written.tempoBpm - 150.0) < 1e-6,
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
        if (s) for (const auto& phrase : s->phrases) if (phrase.used) for (const auto& c : phrase.steps) if (c.note == 60) wrote = true;
        check(wrote, "a played note is recorded as a cell");
        play(48, 60, 50, 62, -1);         // still held
        const bool sounding = p.driverView().view(0).active;
        p.setRecordArm(false);
        play(60, 66, -1, 0, -1);
        check(sounding && !p.driverView().view(0).active, "disarming record stops what was playing through");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
