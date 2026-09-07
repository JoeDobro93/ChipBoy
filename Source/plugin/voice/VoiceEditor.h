// ChipBoy Voice -- the window (UI_DESIGN section 8). The mockup's #voice
// window is the reference: 560 x 420 and fixed, because it lives beside a
// piano roll. Everything here is display and host-parameter plumbing; the
// sound is on the linked ChipBoy track, and the window says so.
#pragma once

#include "plugin/shared/LinkTransport.h"
#include "plugin/ui/Theme.h"
#include "plugin/ui/Widgets.h"
#include "plugin/voice/VoiceProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace chipboy::plugin {

class VoiceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoiceEditor(VoiceProcessor& p);
    ~VoiceEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Field;        ///< a labelled control in the params grid
    struct ParamsGrid;   ///< the grid itself, flow-laid inside the viewport

    void timerCallback() override;
    void refreshInstances();
    void refreshChannels();
    void refreshInstruments(bool force);
    void refreshScope();
    void refreshStatus();
    void refreshNote();
    void applyChannelKind();
    void buildParams();
    void layoutHeader();
    void hint(const juce::String& message);

    juce::RangedAudioParameter& param(const char* id) const;
    juce::String instanceName() const;
    const LinkClient::InstanceInfo* targetInfo() const;
    int instrumentSlot() const;

    VoiceProcessor& processor_;
    ui::ChipBoyLookAndFeel lnf_;
    juce::TooltipWindow tooltips_;

    // header
    ui::Pill statusPill_;
    ui::Pill::Tone statusTone_ = ui::Pill::Tone::Neutral;
    juce::ComboBox instanceBox_, channelBox_;
    std::vector<juce::String> instanceUuids_;
    std::vector<LinkClient::InstanceInfo> instances_;
    juce::String instanceSignature_, channelSignature_;

    // the audio note
    juce::String noteName_;

    // left column
    ui::ScopeView scope_;
    ui::RegisterLine regs_;
    const void* scopeSlot_ = nullptr;
    int modelShown_ = -1;

    // right column
    juce::ComboBox instrumentBox_;
    std::unique_ptr<juce::ParameterAttachment> instrumentAttachment_;
    uint32_t instrumentSerial_ = 0;
    juce::String instrumentTarget_;
    bool instrumentHex_ = false, instrumentLinked_ = false;
    ui::Segmented sourceSeg_;
    juce::TextButton pushButton_, pullButton_, openButton_;

    // the params grid
    juce::Viewport paramsViewport_;
    std::unique_ptr<ParamsGrid> params_;
    std::unique_ptr<juce::ComboBoxParameterAttachment> velocityAttachment_;
    std::array<std::unique_ptr<juce::ComboBoxParameterAttachment>, 2> cmdAttachments_;
    int channelShown_ = -1;

    // painted directly
    juce::Rectangle<int> headerArea_, wordmarkArea_, noteArea_, instrumentLabelArea_, footerArea_;
    juce::String footerText_, lastRequest_, editorMessage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEditor)
};

} // namespace chipboy::plugin
