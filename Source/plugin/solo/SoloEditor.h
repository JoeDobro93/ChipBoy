// ChipBoy Solo -- the window (docs/plan-solo.md section 5, UI_DESIGN
// D-UI-45): 560 x 400, fixed. A header with the channel and the model, a
// page bar, and one page at a time: Main -- the channel's scope with the
// row it plays under it, compact -- or Sounds, Instrument, Tables, Waves and
// Kits (WAV only), Commands, Setup, each taking the whole window. The four
// bank editors are the main window's own panels, through EditorHost.
#pragma once

#include "plugin/main/panels/PanelCommon.h"
#include "plugin/solo/SoloProcessor.h"
#include "plugin/ui/CommandSlot.h"
#include "plugin/ui/Theme.h"
#include "plugin/ui/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

namespace chipboy::plugin {

constexpr int kSoloWidth = 560, kSoloHeight = 552;

/// The Main page: the channel's scope and registers across the top, and the
/// row's fields under them -- SOUND, INST, TABLE, TSP, PAN, LEVEL, CMD 1,
/// CMD 2 -- with the note line last.
class SoloStrip : public juce::Component {
public:
    explicit SoloStrip(SoloProcessor& p);
    ~SoloStrip() override;
    void tick();                       ///< 30 Hz: LED, registers, names, the note line
    void channelChanged();             ///< the channel moved: colour, level control, palettes
    void bankChanged();
    void soloChanged();
    void hexChanged();
    /// The scope's trace, chosen on the Setup page and kept in the state.
    void setScopeTrace(ui::ScopeView::Trace t) { scope_.setTrace(t); }
    std::function<void(ui::SlotKind, int slot)> onOpenSlot;
    std::function<void(int slot)> onOpenSound;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void refreshInstrumentName();
    void refreshSoundName();
    void showInstrumentMenu();
    void showTableMenu();
    void showSoundMenu();
    SoloProcessor& processor_;
    int channel_ = -1;
    ui::Led led_;
    TextLine name_, soundLabel_, instLabel_, tableLabel_, transposeLabel_, panLabel_, levelLabel_, noteLine_;
    ui::ScopeView scope_;
    ui::RegisterLine regs_;
    ui::Stepper sound_, instrument_, table_, transpose_;
    TextLine soundName_, instrumentName_;
    juce::TextButton storeBtn_;
    ui::Stepper level_;
    ui::Segmented pan_;
    std::unique_ptr<SegmentedParam> panParam_;
    std::unique_ptr<ui::CommandSlot> cmd1_, cmd2_;
    const bank::Bank* namesFor_ = nullptr;
    const SoloState* soundsFor_ = nullptr;
    int instrumentShown_ = -1, loadedInstrument_ = 0, soundShown_ = -1;
    int modelShown_ = -1;
    juce::String noteShown_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoloStrip)
};

/// The sixty-four sounds and the key map (plan section 2).
class SoundsPanel : public EditorPanel {
public:
    explicit SoundsPanel(SoloProcessor& p);
    ~SoundsPanel() override;
    RichText contextLine() const override;
    void selectSlot(int slot) override;
    void saveView(juce::ValueTree& v) const override { v.setProperty("slot", slot_, nullptr); }
    void restoreView(const juce::ValueTree& v) override { if (v.hasProperty("slot")) selectSlot(int(v["slot"])); }
    void soloChanged();                ///< the state is a new object
    void channelChanged();             ///< the range moved: the key list is laid again
    void bankChanged() override { rebuildList(); }
    void hexChanged() override;
    void tick() override;
    void resized() override;
private:
    class KeyRow;
    void rebuildList();
    void rebuildKeys();
    void syncKeys();
    juce::String soundText(int slot) const;
    SoloProcessor& processor_;
    ui::SlotList list_;
    TextLine listTitle_, keysTitle_, keysHelp_;
    juce::TextButton recallBtn_, storeBtn_, clearBtn_, defaultKeysBtn_, clearKeysBtn_;
    ui::Toggle keyMap_;
    ScrollBlock keys_;
    std::vector<KeyRow*> keyRows_;
    int slot_ = 1;
    int keysForChannel_ = -1;
    const SoloState* rowsFor_ = nullptr;
    const bank::Bank* rowsBank_ = nullptr;
    int lastSerial_ = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundsPanel)
};

/// The command library and the letter reference (plan section 2).
class CommandsPanel : public EditorPanel {
public:
    explicit CommandsPanel(SoloProcessor& p);
    ~CommandsPanel() override;
    RichText contextLine() const override;
    void soloChanged();
    void channelChanged();
    void hexChanged() override;
    void tick() override;
    void resized() override;
private:
    void rebuildList();
    void rebuildReference();
    SoloProcessor& processor_;
    ui::SlotList list_;
    TextLine listTitle_, refTitle_, rowLine_;
    juce::TextButton use1Btn_, use2Btn_, store1Btn_, store2Btn_, clearBtn_;
    ScrollBlock reference_;
    juce::String rowShown_;
    int entry_ = 1;
    int refForChannel_ = -1;
    const SoloState* rowsFor_ = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommandsPanel)
};

/// The few settings a row does not carry, and the files.
class SetupPanel : public EditorPanel {
public:
    explicit SetupPanel(SoloProcessor& p);
    ~SetupPanel() override;
    RichText contextLine() const override;
    void resized() override;
    /// The scope's trace (0 digital, 1 analog, 2 both), kept as "ui_scope_trace".
    std::function<void(int)> onScopeTrace;
    static int storedTrace(const SoloProcessor& p);
    /// The window's size (0: 100 %, 1: 125 %, 2: 150 %), kept as "ui_scale".
    std::function<void(int)> onScale;
    static int storedScale(const SoloProcessor& p);
private:
    void saveFile();
    void loadFile();
    void loadBankFile();
    SoloProcessor& processor_;
    ScrollBlock scroll_;
    std::unique_ptr<juce::FileChooser> chooser_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetupPanel)
};

class SoloEditor : public juce::AudioProcessorEditor, public ui::EditHistoryHost, private juce::Timer {
public:
    explicit SoloEditor(SoloProcessor& p);
    ~SoloEditor() override;

    enum Tab { Main = 0, Sounds, Instrument, Tables, Waves, Kits, Commands, Setup, kTabs };

    ui::EditHistory& editHistory() override { return processor_.history(); }
    void showTab(int tab);
    void openSlot(ui::SlotKind kind, int slot);
    /// 1.0, 1.25 or 1.5: the whole window in more pixels, as the main window does.
    void setScale(float factor);
    void undo();
    void redo();
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class TabBar;
    void timerCallback() override;
    void channelChanged();
    void refreshContext();
    /// The status line: a message for a few seconds, else the page's context.
    void showMessage(const juce::String& text);
    void refreshHeader();
    void saveView();
    void restoreView();
    bool typing() const;

    SoloProcessor& processor_;
    ui::ChipBoyLookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltips_;
    juce::Component content_;         ///< everything, at 100 %; the scale is a transform on it
    float scale_ = 1.0f;
    // header
    TextLine wordmark_, product_, tempoLabel_, tempo_;
    ui::Segmented channel_, model_;
    juce::TextButton undoBtn_, redoBtn_;
    SoloStrip strip_;
    std::unique_ptr<TabBar> tabs_;
    std::array<std::unique_ptr<EditorPanel>, kTabs> panels_;
    TextLine status_;
    std::unique_ptr<ParamWatch> hexWatch_, channelWatch_;
    int tab_ = 0;
    int channelShown_ = -1;
    const bank::Bank* lastBank_ = nullptr;
    const SoloState* lastSolo_ = nullptr;
    int lastRecall_ = -1;
    RichText lastContext_;
    juce::String tempoShown_, message_;
    uint32_t messageUntil_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoloEditor)
};

} // namespace chipboy::plugin
