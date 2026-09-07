// ChipBoy -- the look (UI_DESIGN section 2, docs/mockups/chipboy_mockup.html).
//
// Colours and fonts are the mockup's; the LookAndFeel draws JUCE's own
// widgets (buttons, combo boxes, sliders, scrollbars, popup menus, tooltips)
// in the same style so nothing looks foreign next to the custom widgets.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace chipboy::ui {

namespace colours {
// docs/mockups/chipboy_mockup.html :root
inline const juce::Colour bg        { 0xff131416 };
inline const juce::Colour panel     { 0xff24272c };
inline const juce::Colour panel2    { 0xff1e2125 };
inline const juce::Colour well      { 0xff17191c };
inline const juce::Colour raised    { 0xff2f3339 };
inline const juce::Colour raisedHi  { 0xff3b4047 };
inline const juce::Colour line      { 0xff3a3e45 };
inline const juce::Colour lineSoft  { 0xff2b2f35 };
inline const juce::Colour text      { 0xffe9e6de };
inline const juce::Colour textMute  { 0xffa3a7ac };
inline const juce::Colour textDim   { 0xff6f747a };
inline const juce::Colour accent    { 0xffc9445f };
inline const juce::Colour accentHi  { 0xffe0577a };
inline const juce::Colour pu1       { 0xff5ec8ff };
inline const juce::Colour pu2       { 0xffffb340 };
inline const juce::Colour wav       { 0xff86e28e };
inline const juce::Colour noi       { 0xffff6f91 };
inline const juce::Colour lcd       { 0xff0b100c };
inline const juce::Colour lcdGrid   { 0xff1a271c };
inline const juce::Colour lcdTrace  { 0xff9bbc0f };
inline const juce::Colour ok        { 0xff6cc46f };
inline const juce::Colour warn      { 0xffe5a53b };
inline const juce::Colour bad       { 0xffe35d6a };
inline const juce::Colour cgb       { 0xffc8a2ff };

inline juce::Colour channel(int ch) { return ch == 0 ? pu1 : ch == 1 ? pu2 : ch == 2 ? wav : noi; }
inline const char* channelName(int ch) { return ch == 0 ? "PU1" : ch == 1 ? "PU2" : ch == 2 ? "WAV" : "NOI"; }
} // namespace colours

/// IBM Plex Sans / IBM Plex Mono / Silkscreen, embedded as BinaryData
/// (OFL, see LICENSING.md); system fallbacks if a face is missing.
struct Fonts {
    static juce::Font sans(float px, bool semibold = false);
    static juce::Font mono(float px);
    static juce::Font pixel(float px);   ///< Silkscreen, for the wordmark and LCD readouts
};

/// Decimal or hex display (spec section 12.2). A display preference, global
/// to the process, message thread only. Widgets read it when they paint.
struct ValueFormat {
    static bool hex();
    static void setHex(bool on);
    static juce::String number(int v);            ///< "12" or "0C"
    static juce::String signedNumber(int v);      ///< "+3", "-12", "0"
    static juce::String byte(int v);              ///< always two hex digits, for register lines
    static juce::String noteName(int midiNote);   ///< "C-4", "F#3"; 255 -> "OFF", 0 -> "---"
};

class ChipBoyLookAndFeel : public juce::LookAndFeel_V4 {
public:
    ChipBoyLookAndFeel();
    ~ChipBoyLookAndFeel() override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isMouseOver, bool isButtonDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isButtonDown) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool isMouseOver, bool isButtonDown) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
    void drawDocumentWindowTitleBar(juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW, const juce::Image* icon, bool drawTitleTextOnLeft) override;
};

/// Window sizes at 100 % (UI_DESIGN sections 2 and 8).
constexpr int kMainWidth = 1180;
constexpr int kMainHeight = 760;
constexpr int kVoiceWidth = 560;
constexpr int kVoiceHeight = 420;

/// Small drawing helpers shared by panels.
namespace draw {
void panel(juce::Graphics&, juce::Rectangle<int> area, juce::Colour fill = colours::panel, juce::Colour border = colours::line, float radius = 4.0f);
void label(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, juce::Justification j = juce::Justification::centredLeft, juce::Colour c = colours::textMute, float px = 11.0f);
void heading(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area);   ///< the mockup's h3: 11 px, upper-case tracking, muted
} // namespace draw

} // namespace chipboy::ui
