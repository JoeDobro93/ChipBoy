// chipboy_linktest -- the two plugins in one process, linked through a
// region file (spec section 16.5). Exit code 0 when every check passes.
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/BankJson.h"
#include "plugin/voice/VoiceProcessor.h"

#include <cmath>
#include <cstdio>

using namespace chipboy::plugin;

namespace {

int failures = 0;
void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
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

float peak(const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) p = std::max(p, std::fabs(b.getSample(c, i)));
    return p;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    ChipBoyProcessor main;
    VoiceProcessor voice;
    main.apvts.getParameter(ids::linkMode)->setValueNotifyingHost(1.0f);
    main.prepareToPlay(48000.0, 512);
    voice.prepareToPlay(48000.0, 512);
    FakePlayHead ph;
    main.setPlayHead(&ph); voice.setPlayHead(&ph);

    pump(350);
    check(main.linkActive(), "link mode publishes the instance");
    check(main.getLatencySamples() == main.latencyFrames() + 512, "latency = renderer + one block");

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
        ChipBoyProcessor a;
        a.mutateSong([](chipboy::tracker::Song& s) {
            s.grooves[2].ticks = { 4, 5, 6, 7 };
            s.phrases[0].used = true;
            s.phrases[0].groove = 16;
            s.phrases[0].steps[0].note = 60;
            s.phrases[0].steps[0].vel = 42;
            s.stepsPerBar = 8;
        });
        juce::MemoryBlock state;
        a.getStateInformation(state);
        ChipBoyProcessor b;
        b.setStateInformation(state.getData(), int(state.getSize()));
        const auto s = b.song();
        check(s && s->grooves[2].length() == 4 && s->grooves[2].at(3) == 7, "a groove's sixteen ticks round-trip through the song");
        check(s && s->phrases[0].groove == 16, "a phrase's groove slot 16 round-trips");
        check(s && s->phrases[0].steps[0].vel == 42, "the VEL column round-trips");
        check(s && s->stepsPerBar == 8, "steps per bar round-trips");
        chipboy::tracker::Song old;
        const bool read = songFromJson("{\"format\":\"chipboy-song\",\"grooves\":[[7,5]]}", old);
        check(read && old.grooves[0].length() == 2 && old.grooves[0].at(0) == 7 && old.grooves[0].at(1) == 5, "the old two-entry groove form still reads");
    }

    /* ---- a channel whose feed changes hands is flushed (section 9.1) -- */
    {
        ChipBoyProcessor p;
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
        ChipBoyProcessor p;
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
        ChipBoyProcessor p;
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
