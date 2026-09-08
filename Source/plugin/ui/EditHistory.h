// ChipBoy -- the undo history every hand edit goes through (UI_DESIGN
// section 2.1, "Editing conventions").
//
// One rule decides what is in it: an edit made *here*, with the mouse or the
// keyboard, is undoable. Host automation, a state restore and anything the
// audio thread does are not -- they never open a transaction, so a knob the
// host is drawing cannot bury what the musician typed.
//
// The processor owns the history so the actions outlive any window; a
// control finds it by walking up to the window it lives in, so two instances
// of the plugin never share one.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace chipboy::ui {

/// What a control asks of the history.
class EditHistory {
public:
    virtual ~EditHistory() = default;

    /// Opens one transaction for a continuous gesture -- a knob drag, a
    /// stepper held down, a fader pulled. Every change until endGesture()
    /// joins it, so the whole gesture is one undo.
    virtual void beginGesture(const juce::String& name) = 0;
    virtual void endGesture() = 0;

    /// Moves a parameter by hand and records it. Its own transaction unless
    /// a gesture is open.
    virtual void setParameter(juce::RangedAudioParameter& p, float denormalised) = 0;
};

/// A window that owns a history: the two editors implement it, and a control
/// finds it with historyFor().
class EditHistoryHost {
public:
    virtual ~EditHistoryHost() = default;
    virtual EditHistory& editHistory() = 0;
};

/// The history of the window this component lives in, or null outside one
/// (a tool that builds a widget on its own, the visualizer window).
EditHistory* historyFor(const juce::Component& c);

/// The history itself: a juce::UndoManager, the naming, and the bracket that
/// makes a gesture one undo. Message thread only.
class UndoHistory : public EditHistory {
public:
    /// Snapshots are the big things in here (a Song is ~300 KB, a Bank ~40 KB
    /// plus its kit samples), so the cap is counted in bytes.
    static constexpr int kMaxUnits = 64 * 1024 * 1024;
    static constexpr int kMinTransactions = 8;

    UndoHistory();

    juce::UndoManager& manager() { return undo_; }

    // --- EditHistory ---------------------------------------------------
    void beginGesture(const juce::String& name) override;
    void endGesture() override;
    void setParameter(juce::RangedAudioParameter& p, float denormalised) override;

    /// A snapshot edit -- a bank or a song the copy-on-write path already
    /// copied. `name` names the transaction; repeating the name of the one
    /// still open joins it, which is how the digits of a typed value, or a
    /// drag down a column, come to one undo.
    void perform(std::unique_ptr<juce::UndoableAction> action, const juce::String& name);

    /// Everything the history holds goes away: a state restore is the host's
    /// edit, not the musician's.
    void clear();

    bool canUndo() const;
    bool canRedo() const;
    juce::String undoName() const;   ///< what Ctrl+Z would put back, or empty
    juce::String redoName() const;
    /// True when it did something; `describe` receives what was undone.
    bool undo();
    bool redo();

private:
    /// "PU1 Instrument 3 -> 5": the parameter's display name and its own text.
    juce::String describe(const juce::RangedAudioParameter& p, float fromNorm, float toNorm) const;

    juce::UndoManager undo_;
    int gestureDepth_ = 0;
    juce::String gestureName_, lastName_;
    /// The parameter a gesture started on and where it started, so the name
    /// reads from where the drag began rather than from its last pixel.
    juce::RangedAudioParameter* gestureParam_ = nullptr;
    float gestureFrom_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UndoHistory)
};

} // namespace chipboy::ui
