// chipboy_uishot -- open the two editors off-screen, play a little through
// the main one so the scopes have something to show, and save PNG snapshots
// of every tab and of the Voice window. Needs a display (Xvfb will do).
//
//   chipboy_uishot <output folder>
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/voice/VoiceProcessor.h"
#include "plugin/ui/Widgets.h"

#include <cstdio>

using namespace chipboy::plugin;

namespace {

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

void save(juce::Component& c, const juce::File& f)
{
    const juce::Image img = c.createComponentSnapshot(c.getLocalBounds(), false, 1.0f);
    juce::FileOutputStream out(f);
    if (out.openedOk()) { out.setPosition(0); out.truncate(); juce::PNGImageFormat().writeImageToStream(img, out); }
    std::printf("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

template <typename T>
T* findChild(juce::Component* c)
{
    if (auto* t = dynamic_cast<T*>(c)) return t;
    for (int i = 0; i < c->getNumChildComponents(); ++i)
        if (auto* t = findChild<T>(c->getChildComponent(i))) return t;
    return nullptr;
}

void set(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    if (auto* p = state.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(value));
    else std::printf("no parameter %s\n", id.toRawUTF8());
}

/// One command in force per channel, so the strips show a slot doing
/// something rather than four empty rows (docs/COMMANDS_AND_TEMPO.md 3).
void setCommands(ChipBoyProcessor& proc)
{
    auto slot = [&proc](int ch, int which, chipboy::bank::Cmd cmd) {
        const auto def = defaultCommand(cmd);
        set(proc.apvts, channelParamId(ch, which == 0 ? ids::cmd1Type : ids::cmd2Type), float(choiceFromCmd(cmd)));
        set(proc.apvts, channelParamId(ch, which == 0 ? ids::cmd1X : ids::cmd2X), float(def.a));
        set(proc.apvts, channelParamId(ch, which == 0 ? ids::cmd1Y : ids::cmd2Y), float(def.b));
    };
    slot(0, 0, chipboy::bank::Cmd::V);   // vibrato on the lead
    slot(1, 0, chipboy::bank::Cmd::E);   // an envelope over the bass
    slot(2, 0, chipboy::bank::Cmd::W);   // another wave under the pad
    slot(3, 0, chipboy::bank::Cmd::R);   // a retrigger on the drum
    slot(3, 1, chipboy::bank::Cmd::O);
}

struct FakePlayHead : juce::AudioPlayHead {
    int64_t frame = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying(true); p.setBpm(120.0); p.setTimeInSamples(frame);
        p.setPpqPosition(double(frame) / 48000.0 * 2.0);
        p.setTimeSignature(TimeSignature { 4, 4 });
        return p;
    }
};

// A few seconds of notes on all four channels, so the strips are live.
void play(ChipBoyProcessor& p, FakePlayHead& ph, int blocks)
{
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        midi.clear();
        if (b % 40 == 0) { midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0); midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 1); }
        if (b % 40 == 20) { midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0); midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8) 90), 1); }
        if (b % 80 == 0) { midi.addEvent(juce::MidiMessage::noteOff(2, 45), 0); midi.addEvent(juce::MidiMessage::noteOn(2, 45, (juce::uint8) 110), 1); }
        if (b % 80 == 0) { midi.addEvent(juce::MidiMessage::noteOff(3, 40), 0); midi.addEvent(juce::MidiMessage::noteOn(3, 40, (juce::uint8) 100), 1); }
        if (b % 20 == 0) { midi.addEvent(juce::MidiMessage::noteOff(4, 60), 0); midi.addEvent(juce::MidiMessage::noteOn(4, 60, (juce::uint8) 100), 1); }
        ph.frame = int64_t(b) * 512;
        p.processBlock(buf, midi);
    }
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "shots");
    outDir.createDirectory();

    chipboy::ui::ScopeView::setOffscreenRefresh(true);
    ChipBoyProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(48000.0, 512);
    setCommands(proc);
    play(proc, ph, 200);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setOpaque(true);
    const bool desktop = argc > 2 && juce::String(argv[2]) == "--desktop";   // a real window: the scopes' timers run
    if (desktop) ed->addToDesktop(0);
    ed->setVisible(true);
    pump(600);
    play(proc, ph, 40); pump(300);
    save(*ed, outDir.getChildFile("main_instrument.png"));

    if (auto* bar = findChild<juce::TabbedButtonBar>(ed.get())) {
        const char* names[] = { "instrument", "tables", "waves", "kits", "phrases", "link", "hardware" };
        for (int i = 1; i < bar->getNumTabs() && i < 7; ++i) {
            bar->setCurrentTabIndex(i);
            pump(300);
            play(proc, ph, 20); pump(200);
            save(*ed, outDir.getChildFile(juce::String("main_") + names[i] + ".png"));
        }
        bar->setCurrentTabIndex(0);
    } else std::printf("no tab bar found\n");

    // The Voice window, linked to the main instance.
    proc.apvts.getParameter(ids::linkMode)->setValueNotifyingHost(1.0f);
    pump(400);
    VoiceProcessor voice;
    voice.prepareToPlay(48000.0, 512);
    voice.setTarget(proc.instanceUuid(), 1);
    set(voice.apvts, juce::String("v_") + ids::cmd1Type, float(choiceFromCmd(chipboy::bank::Cmd::V)));
    set(voice.apvts, juce::String("v_") + ids::cmd1X, 4.0f);
    set(voice.apvts, juce::String("v_") + ids::cmd1Y, 6.0f);
    pump(600);
    std::unique_ptr<juce::AudioProcessorEditor> ved(voice.createEditor());
    ved->setOpaque(true);
    if (desktop) ved->addToDesktop(0);
    ved->setVisible(true);
    pump(600);
    play(proc, ph, 20); pump(300);
    save(*ved, outDir.getChildFile("voice.png"));

    ved.reset();
    ed.reset();
    return 0;
}
