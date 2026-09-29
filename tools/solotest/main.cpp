// chipboy_solotest -- ChipBoy Solo headless (docs/plan-solo.md section 8):
// a note on each channel sounds; a mapped key followed by a note in the same
// block sounds with the recalled sound; the Sound parameter recalls; the
// state and a .cbsolo file round-trip the sounds, the key map, the library
// and the bank; a channel change silences the channel it left.
//
//   chipboy_solotest [--out DIR]
//   chipboy_solotest --load FILE.cbsolo     load a Solo file and say what it holds
#include "plugin/solo/SoloProcessor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace chipboy;
using namespace chipboy::plugin;

namespace {
int failures = 0;
void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

void set(juce::AudioProcessorValueTreeState& s, const juce::String& id, float v)
{
    if (auto* p = s.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    else { std::printf("no parameter %s\n", id.toRawUTF8()); ++failures; }
}
float get(juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* p = s.getParameter(id);
    return p ? p->convertFrom0to1(p->getValue()) : -1.0f;
}

/// Render `blocks` blocks with a MIDI buffer on the first; the peak level.
float run(SoloProcessor& p, juce::MidiBuffer first, int blocks, int& dacOnBlocks)
{
    juce::AudioBuffer<float> buf(2, 512);
    float peak = 0.0f;
    dacOnBlocks = 0;
    for (int b = 0; b < blocks; ++b) {
        juce::MidiBuffer midi;
        if (b == 0) midi = first;
        p.processBlock(buf, midi);
        peak = std::max(peak, buf.getMagnitude(0, 0, 512));
        if (p.driverView().view(p.channel()).active) ++dacOnBlocks;
    }
    return peak;
}

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile("solotest");
    juce::File loadFile;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
        else if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc) loadFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]);
    }
    outDir.createDirectory();
    if (loadFile != juce::File()) {
        // A Solo file, as the Setup page loads it: the bank, the sounds, the
        // library, the key maps and the row; every sound names an instrument
        // the bank holds, and a mapped key plays the first sound.
        SoloProcessor solo;
        solo.prepareToPlay(48000.0, 512);
        juce::String report;
        const bool loaded = solo.loadSoloFile(loadFile, report);
        check(loaded, ("loads: " + report).toRawUTF8());
        const auto s = solo.solo();
        const auto b = solo.bank();
        int sounds = 0, commands = 0, named = 0;
        for (const auto& snd : s->sounds) if (snd.used) { ++sounds; if (snd.inst == 0 || (b && b->instrument(snd.inst) != nullptr)) ++named; }
        for (const auto& c : s->commands) if (c.used) ++commands;
        std::printf("  %d sounds, %d library entries, channel %d, instrument %d\n", sounds, commands, solo.channel(), int(get(solo.apvts, solo.channelParamId(0, ids::instrument))));
        check(sounds >= 1 && named == sounds, "every sound names an instrument the bank holds");
        int lo = 0, hi = 127; solo.noteRange(lo, hi);
        check(s->keyMaps[size_t(solo.channel())][size_t(lo - 1)] == 1, "the key under the floor recalls sound 1");
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, lo - 1, (juce::uint8) 100), 0);
        m.addEvent(juce::MidiMessage::noteOn(1, lo + 24, (juce::uint8) 100), 0);
        int on = 0;
        const float peak = run(solo, m, 40, on);
        check(peak > 0.01f && solo.driverView().view(solo.channel()).instrument == s->sounds[0].inst, "a mapped key and a note: the note plays sound 1's instrument");
        if (failures) std::printf("FAILED %d checks\n", failures);
        else std::printf("PASSED %s\n", loadFile.getFileName().toRawUTF8());
        return failures ? 1 : 0;
    }

    SoloProcessor solo;
    solo.prepareToPlay(48000.0, 512);
    const juce::String inst = solo.channelParamId(0, ids::instrument);

    // --- every channel sounds ---------------------------------------------
    for (int ch = 0; ch < 4; ++ch) {
        set(solo.apvts, solo::ids::channel, float(ch));
        set(solo.apvts, inst, float(ch == 0 ? 1 : ch == 1 ? 3 : ch == 2 ? 7 : 11));   // the factory bank's per-kind defaults
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, ch == 3 ? 40 : 60, (juce::uint8) 100), 0);
        int on = 0;
        const float peak = run(solo, m, 40, on);
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, ch == 3 ? 40 : 60), 0);
        run(solo, off, 40, on);
        char what[96];
        std::snprintf(what, sizeof what, "channel %d sounds (peak %.3f)", ch, double(peak));
        check(peak > 0.01f, what);
    }
    set(solo.apvts, solo::ids::channel, 0.0f);
    set(solo.apvts, inst, 1.0f);
    { int on = 0; run(solo, {}, 4, on); }

    // --- sounds and the key map ---------------------------------------------
    solo.storeSound(1, "one");                         // instrument 1
    set(solo.apvts, inst, 2.0f);
    solo.storeSound(2, "two");                         // instrument 2
    set(solo.apvts, inst, 1.0f);
    {
        const auto s = solo.solo();
        check(s && s->sounds[0].used && s->sounds[0].inst == 1 && s->sounds[1].inst == 2, "two sounds stored from the row");
        int lo = 0, hi = 127; solo.noteRange(lo, hi);
        check(lo == 36 && hi > 100 && s->keyMaps[0][size_t(lo - 1)] == 1 && s->keyMaps[0][size_t(lo - 2)] == 2, "pulse keys: the floor is 36, the key under it is sound 1, the next sound 2");
        check(soloKeyMappable(0, lo - 1) && !soloKeyMappable(0, lo), "a key under the floor is mappable, the floor is not");
    }
    {
        // A mapped key and a note in one block: the note sounds with sound 2.
        int lo = 0, hi = 127; solo.noteRange(lo, hi);
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, lo - 2, (juce::uint8) 100), 0);   // sound 2
        m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        int on = 0;
        const float peak = run(solo, m, 8, on);
        check(peak > 0.01f && solo.driverView().view(0).instrument == 2, "a mapped key then a note in the same block: the note plays the recalled instrument");
        pump(250);                                       // the timer brings the parameters up to the recall
        check(int(get(solo.apvts, inst)) == 2, "the Instrument parameter followed the key's recall");
        check(solo.lastRecall().contains("Sound 2") && solo.lastRecall().contains("key"), "the status line names the sound and the key");
        juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        run(solo, off, 8, on);
        check(solo.lastKey() == lo - 2, "the last key is remembered");
    }
    {
        // The Sound parameter recalls too.
        set(solo.apvts, solo::ids::sound, 1.0f);
        int on = 0; run(solo, {}, 2, on);
        pump(250);
        check(int(get(solo.apvts, inst)) == 1, "the Sound parameter moving to 1 recalled instrument 1");
        // By hand: the row and the Sound parameter, one step.
        solo.recallSound(2, true);
        check(int(get(solo.apvts, inst)) == 2 && int(get(solo.apvts, solo::ids::sound)) == 2, "a hand recall sets the row and the Sound parameter");
        check(solo.history().canUndo(), "and is undoable");
        solo.history().undo();
        check(int(get(solo.apvts, inst)) == 1, "undo puts the row back");
        { int settled = 0; run(solo, {}, 2, settled); pump(250); }
    }
    {
        // A program change is a foot controller's recall: program 1 is sound 2.
        juce::MidiBuffer m; m.addEvent(juce::MidiMessage::programChange(1, 1), 0);
        int on = 0; run(solo, m, 2, on);
        check(solo.recallPending() || solo.driverView().params(0).instrument == 2, "a program change recalls sound 2 on the audio thread");
        pump(250);
        check(int(get(solo.apvts, inst)) == 2 && solo.lastRecall().contains("program change"), "the parameters follow and the status says it was a program change");
        solo.recallSound(1, true);
        { int settled = 0; run(solo, {}, 2, settled); pump(250); }
    }
    {
        // The library.
        set(solo.apvts, solo.channelParamId(0, ids::cmd1Type), float(choiceFromCmd(bank::Cmd::V)));
        set(solo.apvts, solo.channelParamId(0, ids::cmd1X), 8.0f);
        set(solo.apvts, solo.channelParamId(0, ids::cmd1Y), 4.0f);
        solo.storeLibraryCommand(1, 0, "wide vib");
        set(solo.apvts, solo.channelParamId(0, ids::cmd1Type), 0.0f);
        solo.useLibraryCommand(1, 1);
        check(cmdFromChoice(int(get(solo.apvts, solo.channelParamId(0, ids::cmd2Type)))) == bank::Cmd::V && int(get(solo.apvts, solo.channelParamId(0, ids::cmd2X))) == 8, "a library entry goes into CMD 2");
    }

    {
        // Recalling a sound that keeps the instrument and changes a command
        // never retriggers the note (the brief's law): no NRx4 trigger write
        // after the recall, and the command lands at the next tick.
        set(solo.apvts, inst, 1.0f);
        set(solo.apvts, solo.channelParamId(0, ids::cmd1Type), 0.0f);
        set(solo.apvts, solo.channelParamId(0, ids::cmd2Type), 0.0f);
        solo.storeSound(3, "same inst, vib");
        solo.editSolo("vib", [](SoloState& s) { s.sounds[2].cmd1 = bank::Command{ bank::Cmd::V, 6, 4, 0 }; });
        juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        int on = 0; run(solo, m, 8, on);
        std::vector<driver::RegWrite> log;
        solo.setWriteLog(&log);
        solo.recallSound(3, true);
        run(solo, {}, 12, on);
        solo.setWriteLog(nullptr);
        int triggers = 0;
        for (const auto& w : log) if (w.addr == 0xFF14 && (w.value & 0x80)) ++triggers;
        check(triggers == 0 && solo.driverView().view(0).active, "a recall that keeps the instrument does not retrigger the held note");
        check(solo.driverView().slot(0, 0).cmd == bank::Cmd::V, "and its command is in the slot");
        juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        run(solo, off, 8, on);
        pump(250);
    }
    {
        // The sustain pedal holds a released note until it lifts.
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100), 1);
        m.addEvent(juce::MidiMessage::noteOff(1, 62), 100);
        int on = 0; run(solo, m, 20, on);
        check(solo.driverView().view(0).active && solo.driverView().view(0).note == 62, "a note released under the pedal keeps sounding");
        juce::MidiBuffer lift; lift.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        run(solo, lift, 20, on);
        check(!solo.driverView().view(0).active, "and ends when the pedal lifts");
    }
    {
        // The noise channel's keys: 12-127 play, the twelve below select.
        set(solo.apvts, solo::ids::channel, 3.0f);
        int lo = 0, hi = 127; solo.noteRange(lo, hi);
        const auto s = solo.solo();
        check(lo == 12 && hi == 127 && s->keyMaps[3][11] == 1 && s->keyMaps[3][0] == 12, "noise: keys 0-11 select sounds 12 down to 1... counting down from the floor");
        set(solo.apvts, solo::ids::channel, 0.0f);
        int on = 0; run(solo, {}, 4, on);
    }
    {
        // A table change under Live follow starts the table on the held note.
        juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        int on = 0; run(solo, m, 8, on);
        set(solo.apvts, solo.channelParamId(0, ids::table), 1.0f);
        run(solo, {}, 8, on);
        check(solo.driverView().view(0).tableSlot == 1, "a Table change reaches the held note");
        set(solo.apvts, solo.channelParamId(0, ids::table), 0.0f);
        juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        run(solo, off, 8, on);
    }

    // --- state and file round trips -------------------------------------------
    {
        juce::MemoryBlock state;
        solo.getStateInformation(state);
        SoloProcessor other;
        other.prepareToPlay(48000.0, 512);
        other.setStateInformation(state.getData(), int(state.getSize()));
        const auto s = other.solo();
        check(s && s->sounds[1].used && s->sounds[1].name == "two" && s->sounds[1].inst == 2, "the state carries the sounds");
        check(s && s->commands[0].used && s->commands[0].cmd.cmd == bank::Cmd::V, "the state carries the library");
        int lo = 0, hi = 127; other.noteRange(lo, hi);
        check(s && s->keyMaps[0][size_t(lo - 2)] == 2 && s->keyMaps[2][size_t(23)] == 1, "the state carries the key maps, one per channel");
        check(other.bank() && other.bank()->instrument(1) != nullptr && other.bank()->instrument(1)->name == solo.bank()->instrument(1)->name, "the state carries the bank");
        check(int(get(other.apvts, solo.channelParamId(0, ids::cmd2X))) == 8, "the state carries the row");
    }
    {
        const juce::File f = outDir.getChildFile("roundtrip.cbsolo");
        check(solo.saveSoloFile(f), "a .cbsolo file is written");
        SoloProcessor other;
        other.prepareToPlay(48000.0, 512);
        juce::String report;
        check(other.loadSoloFile(f, report), ("the file loads: " + report).toRawUTF8());
        const auto s = other.solo();
        check(s && s->sounds[0].used && s->sounds[0].name == "one", "the file carries the sounds");
        check(int(get(other.apvts, solo.channelParamId(0, ids::cmd2X))) == 8 && int(get(other.apvts, solo::ids::channel)) == 0, "the file carries the row and the channel");
        check(other.history().canUndo(), "loading a file is one undo step");
    }

    // --- a channel change silences the channel it left ------------------------
    {
        juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        int on = 0; run(solo, m, 8, on);
        check(solo.driverView().view(0).active, "PU1 sounds before the change");
        set(solo.apvts, solo::ids::channel, 1.0f);
        run(solo, {}, 8, on);
        check(!solo.driverView().view(0).active, "after the change to PU2, PU1 is silent");
        set(solo.apvts, solo::ids::channel, 0.0f);
        juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        run(solo, off, 8, on);
    }
    {
        // Live follow: a held note takes an instrument change at once.
        juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        int on = 0; run(solo, m, 8, on);
        set(solo.apvts, inst, 2.0f);
        run(solo, {}, 8, on);
        check(solo.driverView().view(0).active && solo.driverView().view(0).instrument == 2, "a held note reloads when the Instrument parameter moves (Live follow)");
    }

    std::printf(failures ? "FAILED %d checks\n" : "PASSED chipboy_solotest\n", failures);
    return failures ? 1 : 0;
}
