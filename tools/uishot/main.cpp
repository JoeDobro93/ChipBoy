// chipboy_uishot -- open the two editors off-screen, play a little through
// the main one so the scopes have something to show, and save PNG snapshots
// of every tab, of the main window stretched 200 px taller, and of the Voice
// window. Every shot reports its scrolling panes, so a run says plainly
// whether a tab fits the window. Needs a display (Xvfb will do).
//
//   chipboy_uishot <output folder> [--desktop] [--song <file.cbsong>]
//                  [--hybrid] [--shaped] [--scope-check] [--tab-switch]
//
// --song opens a .cbsong in a tab of its own before the editor opens and
// leaves the processor without a play head, so the plugin owns the transport
// (docs/COMMANDS_AND_TEMPO.md section 16): the Tracker tab's buttons are
// live, the song plays on the plugin's own clock while the shots are taken,
// and the strip shows the two tabs -- the empty song the plugin starts with
// and the one that was opened (section 18).
//
// --hybrid puts two of the song's channels on Hybrid, so a shot shows the
// three-way playback switch and the strip controls a Hybrid channel does not
// read (section 20).
//
// --shaped gives the first instrument a shaped envelope, so the Instrument
// tab's shot shows the ADSR graph and its curves rather than the chip's ramp
// (section 27); it is also the tallest the tab gets, so the pane report says
// whether the form still fits.
//
// --tab-switch checks that the other tabs follow the active song tab
// (section 18): with two songs open it shots the Grooves, Instrument,
// Tables and Waves panes on one tab, switches, and shots them again --
// every one of them has to have redrawn.
//
// --scope-check holds one note per channel and renders each channel scope
// twice, a fifth of a second apart, comparing the two pictures pixel for
// pixel: a period-locked scope on a steady tone must draw the same picture
// every frame (section 22). It prints a line per channel and returns 1 if a
// period-locked one moved.
#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/SongFiles.h"
#include "plugin/voice/VoiceProcessor.h"
#include "plugin/ui/Widgets.h"

#include <cstdio>
#include <iterator>
#include <vector>

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

/// Every scrolling pane on the visible tab: what it is showing, and whether
/// it had to put a scrollbar up. The Instrument tab must fit at the default
/// size (UI_DESIGN section 2).
void reportPanes(juce::Component& c, const char* what)
{
    if (auto* v = dynamic_cast<juce::Viewport*>(&c)) {
        const auto* held = v->getViewedComponent();
        std::printf("  %s: pane %d x %d holds %d -- %s\n", what, v->getWidth(), v->getHeight(),
                    held != nullptr ? held->getHeight() : 0, v->getVerticalScrollBar().isVisible() ? "SCROLLS" : "fits");
    }
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* k = c.getChildComponent(i); k->isVisible()) reportPanes(*k, what);
}

template <typename T>
T* findChild(juce::Component* c)
{
    if (auto* t = dynamic_cast<T*>(c)) return t;
    for (int i = 0; i < c->getNumChildComponents(); ++i)
        if (auto* t = findChild<T>(c->getChildComponent(i))) return t;
    return nullptr;
}

/// Every component of a kind, in the order they were added -- the four
/// channel scopes, left to right.
template <typename T>
void collect(juce::Component* c, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*>(c)) out.push_back(t);
    for (int i = 0; i < c->getNumChildComponents(); ++i) collect<T>(c->getChildComponent(i), out);
}

/// How many pixels of two snapshots of one component differ.
int pixelsDiffering(const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return -1;
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) ++n;
    return n;
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

/// Select, in the visible tab's slot list, a table a channel is running, so
/// the shot shows the playhead of section 32. Silent when none is.
void showRunningTable(ChipBoyProcessor& proc, FakePlayHead& ph, juce::Component& editor)
{
    // A table runs for as long as its rows last, so the song is played on in
    // small steps until one is under way.
    int slot = 0;
    for (int attempt = 0; attempt < 40 && slot == 0; ++attempt) {
        for (int ch = 0; ch < 4; ++ch) {
            int s = 0, row = -1;
            uint32_t run = 0;
            chipboy::plugin::unpackTableRun(proc.scopes().tableRun[size_t(ch)].load(), s, row, run);
            if (row >= 0 && s > 0) { slot = s; break; }
        }
        if (slot == 0) { play(proc, ph, 2); pump(40); }
    }
    if (slot == 0) { std::printf("  tables: no channel is running a table\n"); return; }
    // The editor is off the desktop here, so isShowing() is false for every
    // component: the visible tab is the one whose chain is all setVisible.
    const auto onScreen = [](const juce::Component* c) {
        for (; c != nullptr; c = c->getParentComponent()) if (!c->isVisible()) return false;
        return true;
    };
    std::vector<chipboy::ui::SlotList*> lists;
    collect<chipboy::ui::SlotList>(&editor, lists);
    for (auto* l : lists)
        if (onScreen(l)) { l->setSelected(slot); break; }
    pump(200);
    std::printf("  tables: showing table %d, which a channel is running\n", slot);
}

/// One note per channel, held: the steady tone the scopes must hold still on.
void playSteady(ChipBoyProcessor& p, FakePlayHead& ph, int blocks, int64_t& frame)
{
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        midi.clear();
        if (b == 0)
            for (int ch = 1; ch <= 4; ++ch)
                midi.addEvent(juce::MidiMessage::noteOn(ch, ch == 4 ? 48 : 52 + 5 * ch, (juce::uint8) 100), 1);
        ph.frame = frame;
        frame += 512;
        p.processBlock(buf, midi);
    }
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String out = "shots", songPath;
    bool desktop = false;                       // a real window: the scopes' timers run
    bool hybrid = false, shaped = false, scopeCheck = false, tabSwitch = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        if (a == "--desktop") desktop = true;
        else if (a == "--hybrid") hybrid = true;
        else if (a == "--shaped") shaped = true;
        else if (a == "--scope-check") scopeCheck = true;
        else if (a == "--tab-switch") tabSwitch = true;
        else if (a == "--song" && i + 1 < argc) songPath = argv[++i];
        else if (!a.startsWith("--")) out = a;
    }
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile(out);
    outDir.createDirectory();

    chipboy::ui::ScopeView::setOffscreenRefresh(true);
    ChipBoyProcessor proc;
    FakePlayHead ph;
    // A song of its own means no host: the plugin runs the transport, which
    // is what the Tracker tab's buttons show (section 16).
    const bool ownTransport = songPath.isNotEmpty();
    if (!ownTransport) proc.setPlayHead(&ph);
    proc.prepareToPlay(48000.0, 512);
    if (ownTransport) {
        // A song opens in a tab of its own (section 18), so the strip shows
        // the empty song the plugin starts with beside the one that was
        // opened, and the opened one is the live tab.
        const juce::File songFile = juce::File::getCurrentWorkingDirectory().getChildFile(songPath);
        chipboy::plugin::SongReport report;
        if (!proc.openSongFileInTab(songFile, report)) { std::printf("could not read %s\n", songFile.getFullPathName().toRawUTF8()); return 1; }
        std::printf("opened %s in tab %d of %d -- bank %s%s, %d instruments, %d differences\n", songFile.getFileName().toRawUTF8(),
                    proc.activeTab() + 1, proc.tabCount(), report.bankName.toRawUTF8(), report.hasBank ? " (its own)" : "",
                    report.instrumentsUsed, report.differences.size() + report.missing.size());
        // Two of the four on Hybrid: MIDI notes, the song's cells for
        // everything else (section 20).
        if (hybrid)
            proc.editSong("Hybrid channels", [](chipboy::tracker::Song& s) {
                s.noteSource[1] = chipboy::tracker::NoteSource::Hybrid;
                s.noteSource[3] = chipboy::tracker::NoteSource::Hybrid;
            });
        proc.transportPlay();
    }
    // --scope-check: two frames of a steady tone, compared (section 22).
    if (scopeCheck) {
        chipboy::ui::ScopeView::setOffscreenRefresh(true);
        std::unique_ptr<juce::AudioProcessorEditor> ced(proc.createEditor());
        ced->setOpaque(true);
        ced->setVisible(true);
        pump(400);
        int64_t frame = 0;
        playSteady(proc, ph, 120, frame);        // a second and a bit of one held note each
        pump(300);
        std::vector<chipboy::ui::ScopeView*> scopes;
        collect<chipboy::ui::ScopeView>(ced.get(), scopes);
        std::vector<juce::Image> first;
        for (auto* s : scopes) first.push_back(s->createComponentSnapshot(s->getLocalBounds(), false, 1.0f));
        playSteady(proc, ph, 20, frame);         // a fifth of a second on
        pump(300);
        int bad = 0;
        static const char* kNames[] = { "PU1", "PU2", "WAV", "NOI" };
        for (size_t i = 0; i < scopes.size(); ++i) {
            const juce::Image now = scopes[i]->createComponentSnapshot(scopes[i]->getLocalBounds(), false, 1.0f);
            const int diff = pixelsDiffering(first[i], now);
            const bool locked = i < 3;           // noise keeps a fixed window
            const char* name = i < 4 ? kNames[i] : "extra";
            std::printf("  scope %s (%s): %d pixels differ%s\n", name, locked ? "period-locked" : "fixed window", diff,
                        locked && diff != 0 ? "   <-- it moved" : "");
            if (locked && diff != 0) ++bad;
        }
        ced.reset();
        std::printf("scope-check: %s\n", bad == 0 ? "every period-locked scope drew the same picture twice" : "a period-locked scope moved");
        return bad == 0 ? 0 : 1;
    }


    // --tab-switch: every other tab reads the active song and bank through
    // song() and bank(), so switching tabs has to redraw all of them.
    if (tabSwitch) {
        chipboy::ui::ScopeView::setOffscreenRefresh(true);
        std::unique_ptr<juce::AudioProcessorEditor> ted(proc.createEditor());
        ted->setOpaque(true);
        ted->setVisible(true);
        pump(400);
        auto* bar = findChild<juce::TabbedButtonBar>(ted.get());
        if (bar == nullptr || proc.tabCount() < 2) { std::printf("tab-switch needs a tab bar and two songs\n"); return 1; }
        // The two tabs start on copies of the same factory bank, and a new
        // song's grooves are the factory ones, so they would draw the same
        // whether the panels followed or not. Mark the other tab's own bank
        // and song first -- which is itself a check that an edit lands in
        // the tab it was made in (section 18).
        const int demoTab = proc.activeTab();
        proc.setActiveTab(demoTab == 0 ? 1 : 0);
        proc.editBank("mark", [](chipboy::bank::Bank& b) {
            b.instruments[0].name = "Marked";
            b.tables[0].used = true; b.tables[0].name = "Marked"; b.tables[0].steps[0].vol = 9;
            b.waves[0].used = true; b.waves[0].name = "Marked";
            if (!b.waves[0].frames.empty()) b.waves[0].frames[0].s[0] = 15;
            b.kits[0].used = true; b.kits[0].name = "Marked";
        });
        proc.editSong("mark", [](chipboy::tracker::Song& s) { s.grooves[0].ticks = { 9, 3 }; });
        proc.setActiveTab(demoTab);
        pump(400);
        const int panes[] = { 0, 1, 2, 3, 4 };    // Instrument, Tables, Grooves, Waves, Kits
        static const char* kPaneNames[] = { "Instrument", "Tables", "Grooves", "Waves", "Kits" };
        std::vector<juce::Image> before;
        for (int i : panes) { bar->setCurrentTabIndex(i); pump(250); before.push_back(ted->createComponentSnapshot(ted->getLocalBounds(), false, 1.0f)); }
        proc.setActiveTab(proc.activeTab() == 0 ? 1 : 0);
        pump(400);                                 // the editor's timer notices the new song and bank
        int stuck = 0;
        for (size_t k = 0; k < before.size(); ++k) {
            bar->setCurrentTabIndex(panes[k]);
            pump(250);
            const juce::Image now = ted->createComponentSnapshot(ted->getLocalBounds(), false, 1.0f);
            const int diff = pixelsDiffering(before[k], now);
            std::printf("  %s: %d pixels differ after the tab switch%s\n", kPaneNames[k], diff, diff == 0 ? "   <-- it did not follow" : "");
            if (diff == 0) ++stuck;
        }
        ted.reset();
        std::printf("tab-switch: %s\n", stuck == 0 ? "every tab followed the active song" : "a tab did not follow");
        return stuck == 0 ? 0 : 1;
    }

    // A shaped envelope on the first instrument: the graph then draws an
    // ADSR with its curves, and the form is at its tallest (section 27).
    if (shaped)
        proc.editBank("Shaped envelope", [](chipboy::bank::Bank& b) {
            auto& i = b.instruments[0];
            i.env.mode = chipboy::bank::EnvMode::Shaped;
            i.env.attackTicks = 10; i.env.peak = 15;
            i.env.decayTicks = 26; i.env.sustain = 8;
            i.env.releaseTicks = 40;
            i.env.attackCurve = chipboy::bank::EnvCurve::Exponential;
            i.env.decayCurve = chipboy::bank::EnvCurve::Logarithmic;
            i.env.releaseCurve = chipboy::bank::EnvCurve::Linear;
        });

    setCommands(proc);
    play(proc, ph, 200);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setOpaque(true);
    if (desktop) ed->addToDesktop(0);
    ed->setVisible(true);
    pump(600);
    play(proc, ph, 40); pump(300);
    save(*ed, outDir.getChildFile("main_instrument.png"));
    reportPanes(*ed, "instrument");

    if (auto* bar = findChild<juce::TabbedButtonBar>(ed.get())) {
        const char* names[] = { "instrument", "tables", "grooves", "waves", "kits", "tracker", "link", "hardware" };
        for (int i = 1; i < bar->getNumTabs() && i < int(std::size(names)); ++i) {
            bar->setCurrentTabIndex(i);
            pump(300);
            play(proc, ph, 20); pump(200);
            // The Tables tab shows a table's running row (section 32), so the
            // shot lands on a table a channel is really running.
            if (juce::String(names[i]) == "tables") showRunningTable(proc, ph, *ed);
            save(*ed, outDir.getChildFile(juce::String("main_") + names[i] + ".png"));
            reportPanes(*ed, names[i]);
        }
        bar->setCurrentTabIndex(0);
        pump(200);
    } else std::printf("no tab bar found\n");

    // The window stretches in height: the top stays where it is and the
    // extra 200 px are the editor pane's (UI_DESIGN section 2).
    ed->setSize(ed->getWidth(), ed->getHeight() + 200);
    pump(300);
    play(proc, ph, 20); pump(200);
    save(*ed, outDir.getChildFile("main_instrument_tall.png"));
    reportPanes(*ed, "instrument tall");

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
    reportPanes(*ved, "voice");

    ved.reset();
    ed.reset();
    return 0;
}
