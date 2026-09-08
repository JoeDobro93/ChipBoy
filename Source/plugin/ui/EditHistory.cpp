#include "plugin/ui/EditHistory.h"

#include <cmath>

namespace chipboy::ui {

namespace {

const juce::String kArrow = juce::String(juce::CharPointer_UTF8(" \xe2\x86\x92 "));

/// One parameter moved by hand. It applies with setValueNotifyingHost, so
/// undoing looks to the host exactly like the gesture that made the change.
struct ParamAction : juce::UndoableAction {
    juce::RangedAudioParameter& param;
    float before, after;             ///< normalised

    ParamAction(juce::RangedAudioParameter& p, float from, float to) : param(p), before(from), after(to) {}

    bool perform() override { apply(after); return true; }
    bool undo() override { apply(before); return true; }
    int getSizeInUnits() override { return int(sizeof(ParamAction)); }

    /// Two moves of the same parameter inside one transaction are one move.
    juce::UndoableAction* createCoalescedAction(juce::UndoableAction* next) override
    {
        if (auto* n = dynamic_cast<ParamAction*>(next))
            if (&n->param == &param) return new ParamAction(param, before, n->after);
        return nullptr;
    }

private:
    void apply(float v) const
    {
        param.beginChangeGesture();
        param.setValueNotifyingHost(v);
        param.endChangeGesture();
    }
};

} // namespace

EditHistory* historyFor(const juce::Component& c)
{
    if (auto* host = c.findParentComponentOfClass<EditHistoryHost>()) return &host->editHistory();
    return nullptr;
}

/* ------------------------------------------------------- UndoHistory */

UndoHistory::UndoHistory()
{
    undo_.setMaxNumberOfStoredUnits(kMaxUnits, kMinTransactions);
}

void UndoHistory::beginGesture(const juce::String& name)
{
    if (gestureDepth_++ > 0) return;
    gestureName_ = name;
    gestureParam_ = nullptr;
    undo_.beginNewTransaction(name);
    lastName_ = name;
}

void UndoHistory::endGesture()
{
    if (gestureDepth_ > 0) --gestureDepth_;
    if (gestureDepth_ > 0) return;
    gestureParam_ = nullptr;
    gestureName_.clear();
    lastName_.clear();          // the next edit is a new one whatever it is
}

juce::String UndoHistory::describe(const juce::RangedAudioParameter& p, float fromNorm, float toNorm) const
{
    auto& prm = const_cast<juce::RangedAudioParameter&>(p);
    const juce::String from = prm.getText(fromNorm, 24).trim(), to = prm.getText(toNorm, 24).trim();
    return prm.getName(64) + " " + from + kArrow + to;
}

void UndoHistory::setParameter(juce::RangedAudioParameter& p, float denormalised)
{
    const float before = p.getValue();
    const float after = juce::jlimit(0.0f, 1.0f, p.convertTo0to1(denormalised));
    if (std::abs(after - before) < 1.0e-7f) return;

    // Inside a gesture the name reads from where the drag began, so a knob
    // pulled across its range says what it really did.
    float from = before;
    if (gestureDepth_ > 0) {
        if (gestureParam_ == &p) from = gestureFrom_;
        else { gestureParam_ = &p; gestureFrom_ = before; }
    }
    const juce::String name = describe(p, from, after);
    if (gestureDepth_ == 0) undo_.beginNewTransaction(name);
    undo_.perform(new ParamAction(p, before, after), name);
    lastName_ = name;
}

void UndoHistory::perform(std::unique_ptr<juce::UndoableAction> action, const juce::String& name)
{
    if (action == nullptr) return;
    if (gestureDepth_ == 0 && name != lastName_) undo_.beginNewTransaction(name);
    undo_.perform(action.release(), name);
    lastName_ = name;
}

void UndoHistory::clear()
{
    undo_.clearUndoHistory();
    gestureDepth_ = 0;
    gestureParam_ = nullptr;
    gestureName_.clear();
    lastName_.clear();
}

bool UndoHistory::canUndo() const { return const_cast<juce::UndoManager&>(undo_).canUndo(); }
bool UndoHistory::canRedo() const { return const_cast<juce::UndoManager&>(undo_).canRedo(); }
juce::String UndoHistory::undoName() const { return const_cast<juce::UndoManager&>(undo_).getUndoDescription(); }
juce::String UndoHistory::redoName() const { return const_cast<juce::UndoManager&>(undo_).getRedoDescription(); }

bool UndoHistory::undo()
{
    lastName_.clear();
    return undo_.undo();
}

bool UndoHistory::redo()
{
    lastName_.clear();
    return undo_.redo();
}

} // namespace chipboy::ui
