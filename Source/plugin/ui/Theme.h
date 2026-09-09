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
// derived tones the mockup uses as rgba()
inline const juce::Colour accentSoft  { 0x2ec9445f };   ///< --accent-soft: accent at 18 %
inline const juce::Colour playRow     { 0x29c9445f };   ///< tr.play: accent at 16 %
inline const juce::Colour scopeBorder { 0xff0a0e0b };   ///< .scope border
inline const juce::Colour videoBorder { 0xff141414 };   ///< .video-grid canvas border
inline const juce::Colour videoGrid   { 0xff161616 };   ///< the visualizer's grid on a black ground
inline const juce::Colour ledOff      { 0xff2a2d32 };
inline const juce::Colour black       { 0xff000000 };

inline juce::Colour channel(int ch) { return ch == 0 ? pu1 : ch == 1 ? pu2 : ch == 2 ? wav : noi; }
inline const char* channelName(int ch) { return ch == 0 ? "PU1" : ch == 1 ? "PU2" : ch == 2 ? "WAV" : "NOI"; }
} // namespace colours

/// IBM Plex Sans / IBM Plex Mono / Silkscreen, embedded as BinaryData
/// (OFL, see LICENSING.md); system fallbacks if a face is missing.
struct Fonts {
    static juce::Font sans(float px, bool semibold = false);
    static juce::Font mono(float px);
    static juce::Font pixel(float px);   ///< Silkscreen, for the wordmark and LCD readouts
    static juce::Font caption(float px = 10.0f, bool semibold = false);   ///< the mockup's .label: sans with .12em tracking (draw upper-case)
    static bool embedded();              ///< true when the OFL faces were found in BinaryData
};

/// Decimal or hex display (spec section 12.2). A display preference, global
/// to the process, message thread only. Widgets read it when they paint.
struct ValueFormat {
    static bool hex();
    static void setHex(bool on);
    static juce::String number(int v);            ///< "12" or "0C"
    static juce::String signedNumber(int v);      ///< "+3", "-12", "0"
    /// Hex counts like LSDj (docs/COMMANDS_AND_TEMPO.md section 52): a slot
    /// shows from 00, a row or step from 00, a transpose as its two's
    /// complement byte. Decimal stays 1-based and signed.
    static juce::String slot(int slot);           ///< slot 1: "1" in Decimal, "00" in Hex
    static juce::String index(int zeroBased);     ///< row 0: "1" in Decimal, "00" in Hex
    static juce::String transpose(int v);         ///< "-32" in Decimal, "E0" in Hex
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

    // extra hooks so JUCE's own widgets sit in the same style
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    int getTextButtonWidthToFitText(juce::TextButton&, int buttonHeight) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    int getDefaultScrollbarWidth() override;
    int getMinimumScrollbarThumbSize(juce::ScrollBar&) override;
    int getScrollbarButtonSize(juce::ScrollBar&) override;
    int getSliderThumbRadius(juce::Slider&) override;
    int getTabButtonOverlap(int tabDepth) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
};

/// Window sizes at 100 % (UI_DESIGN sections 2 and 8). The main window is
/// fixed in width and stretches in height: kMainHeight is the default and
/// the smallest height, and it fits a 1080p screen --
/// header 54 + mixer row 370 + tab bar 34 + editor pane 536 + status 26.
/// The pane is 512 and its 2 x 12 padding: 512 is what the Phrases lane's
/// sixteen steps need under its head (512), with the Instrument tab's tallest
/// content (a Pulse instrument, every card: 474) inside it.
constexpr int kMainWidth = 1280;   // UI_DESIGN D-UI-19
constexpr int kMainHeight = 1020;
constexpr int kMainMaxHeight = 2400;   ///< as tall as a screen is ever likely to be at 100 %
constexpr int kVoiceWidth = 560;
constexpr int kVoiceHeight = 420;

/// Small drawing helpers shared by panels.
namespace draw {
void panel(juce::Graphics&, juce::Rectangle<int> area, juce::Colour fill = colours::panel, juce::Colour border = colours::line, float radius = 4.0f);
void label(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, juce::Justification j = juce::Justification::centredLeft, juce::Colour c = colours::textMute, float px = 11.0f);
void heading(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area);   ///< the mockup's h3: 11 px, upper-case tracking, muted
void caption(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, juce::Justification j = juce::Justification::centredLeft, juce::Colour c = colours::textDim, float px = 10.0f);   ///< the mockup's .label: upper-case, tracked, dim
void switchTrack(juce::Graphics&, juce::Rectangle<float> area, bool on, bool hover, bool enabled);   ///< the mockup's .switch input: a 30 x 16 track with a thumb
void dial(juce::Graphics&, juce::Rectangle<float> area, float proportion, juce::Colour arc, juce::Colour pointer, bool enabled);   ///< the mockup's .knob .dial with its 270-degree arc
float textWidth(const juce::Font&, const juce::String& text);
} // namespace draw

} // namespace chipboy::ui
