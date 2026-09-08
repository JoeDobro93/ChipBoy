// ChipBoy -- bank and song files (spec section 14.1): JSON documents with
// their own extensions, picked through the host's file dialogs.
#pragma once

#include "core/Bank/Bank.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace chipboy::plugin {

/// A save dialog, then bankToJson to a *.chipboy file. `done` runs on the
/// message thread; ok is false when cancelled or when the write failed.
void saveBankAs(juce::Component* parent, const bank::Bank& bank, const juce::String& suggestedName, std::function<void(bool ok, juce::File)> done);
/// An open dialog, then bankFromJson. Null on cancel, or on a file that does not parse.
void loadBank(juce::Component* parent, std::function<void(std::unique_ptr<bank::Bank>, juce::File)> done);
/// Songs have their own file (plugin/shared/SongFiles.h): they carry the
/// bank's name and the names of the instrument slots they use, which a plain
/// bankToJson-shaped writer cannot.
/// Documents/ChipBoy/Banks, created on demand; where the dialogs start.
juce::File banksFolder();

} // namespace chipboy::plugin
