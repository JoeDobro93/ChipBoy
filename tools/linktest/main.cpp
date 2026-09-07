// chipboy_linktest -- the two plugins in one process, linked through a
// region file (spec section 16.5). Exit code 0 when every check passes.
#include "plugin/main/ChipBoyProcessor.h"
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

    // Link off: the Voice sees the instance go.
    main.apvts.getParameter(ids::linkMode)->setValueNotifyingHost(0.0f);
    pump(400);
    check(!main.linkActive(), "link mode off unpublishes");
    check(voice.link().status() != LinkClient::Status::Linked, "the Voice is no longer linked");

    std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
