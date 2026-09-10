// ChipBoy -- the LSDj import dialog (UI_DESIGN D-UI-22, docs/plan-lsdj-import.md
// section 5): the songs a .sav holds, each with a checkbox, the working song
// first; a Version dropdown over the models; Import opens a tab per song.
#pragma once

#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/shared/LsdjImport.h"
#include "plugin/ui/Widgets.h"

#include <functional>
#include <memory>
#include <vector>

namespace chipboy::plugin {

class LsdjImportDialog : public juce::Component {
public:
    /// What the import did, for the status line: how many songs, and the
    /// notes (already shown in a message box when there were any).
    struct Outcome { int songs = 0; juce::StringArray notes; };

    /// Opens the dialog over `parent`'s window; `done` runs after an import.
    static void show(ChipBoyProcessor& processor, SavePreview preview, juce::Component* parent, std::function<void(const Outcome&)> done);

    LsdjImportDialog(ChipBoyProcessor& processor, SavePreview preview, std::function<void(const Outcome&)> done);
    ~LsdjImportDialog() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr int kWidth = 560;
    int preferredHeight() const;

private:
    struct Row;
    void runImport();
    void close();
    const lsdj::LsdjModel& modelFor(int formatVersion) const;

    ChipBoyProcessor& processor_;
    SavePreview preview_;
    std::function<void(const Outcome&)> done_;
    std::vector<std::unique_ptr<Row>> rows_;
    juce::Label title_, versionLabel_, romLine_;
    juce::ComboBox version_;
    juce::TextButton import_, cancel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LsdjImportDialog)
};

} // namespace chipboy::plugin
