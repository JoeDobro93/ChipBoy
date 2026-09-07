// ChipBoy -- the look: fonts, value formatting, the LookAndFeel and the
// small drawing helpers (UI_DESIGN section 2, docs/mockups/chipboy_mockup.html).
#include "plugin/ui/Theme.h"

#if CHIPBOY_HAVE_ASSETS
#include "BinaryData.h"
#endif

#include <cmath>

namespace chipboy::ui {

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------
namespace {

struct Faces {
    juce::Typeface::Ptr sans, sansSemi, mono, pixel;
};

juce::Typeface::Ptr faceFrom(const char* data, int size)
{
    if (data == nullptr || size <= 0) return nullptr;
    return juce::Typeface::createSystemTypefaceFor(data, size_t(size));
}

const Faces& faces()
{
    static const Faces f = [] {
        Faces r;
#if CHIPBOY_HAVE_ASSETS
        r.sans     = faceFrom(assets::IBMPlexSansRegular_ttf,  assets::IBMPlexSansRegular_ttfSize);
        r.sansSemi = faceFrom(assets::IBMPlexSansSemiBold_ttf, assets::IBMPlexSansSemiBold_ttfSize);
        r.mono     = faceFrom(assets::IBMPlexMonoRegular_ttf,  assets::IBMPlexMonoRegular_ttfSize);
        r.pixel    = faceFrom(assets::SilkscreenRegular_ttf,   assets::SilkscreenRegular_ttfSize);
#endif
        return r;
    }();
    return f;
}

// The mockup's sizes are CSS pixels, i.e. em sizes: JUCE's "point height" is
// the same measure, whereas its legacy "height" is ascent + descent.
juce::Font fontFor(const juce::Typeface::Ptr& face, const juce::String& fallbackName, bool bold, float px)
{
    const float pt = juce::jmax(1.0f, px);
    if (face != nullptr)
        return juce::Font(juce::FontOptions(face).withPointHeight(pt));
    return juce::Font(juce::FontOptions().withName(fallbackName).withStyle(bold ? "Bold" : "Regular").withPointHeight(pt));
}

} // namespace

juce::Font Fonts::sans(float px, bool semibold)
{
    const auto& f = faces();
    if (semibold && f.sansSemi != nullptr) return fontFor(f.sansSemi, {}, true, px);
    if (semibold && f.sans != nullptr) return fontFor(f.sans, {}, true, px).boldened();
    return fontFor(f.sans, juce::Font::getDefaultSansSerifFontName(), semibold, px);
}
juce::Font Fonts::mono(float px)   { return fontFor(faces().mono, juce::Font::getDefaultMonospacedFontName(), false, px); }
juce::Font Fonts::pixel(float px)  { return faces().pixel != nullptr ? fontFor(faces().pixel, {}, false, px) : mono(px); }
juce::Font Fonts::caption(float px, bool semibold) { return sans(px, semibold).withExtraKerningFactor(0.12f); }
bool Fonts::embedded()             { return faces().sans != nullptr && faces().mono != nullptr; }

// ---------------------------------------------------------------------------
// value format
// ---------------------------------------------------------------------------
namespace {
bool gHex = false;

juce::String hexDigits(int v)   // v >= 0, at least two digits
{
    auto s = juce::String::toHexString(v).toUpperCase();
    return s.length() < 2 ? "0" + s : s;
}
} // namespace

bool ValueFormat::hex()          { return gHex; }
void ValueFormat::setHex(bool on) { gHex = on; }

juce::String ValueFormat::number(int v)
{
    if (!gHex) return juce::String(v);
    return v < 0 ? "-" + hexDigits(-v) : hexDigits(v);
}
juce::String ValueFormat::signedNumber(int v)
{
    if (!gHex) return v > 0 ? "+" + juce::String(v) : juce::String(v);
    if (v > 0) return "+" + hexDigits(v);
    if (v < 0) return "-" + hexDigits(-v);
    return "00";
}
juce::String ValueFormat::byte(int v)
{
    return juce::String::toHexString(v & 0xFF).toUpperCase().paddedLeft('0', 2);
}
juce::String ValueFormat::noteName(int n)
{
    if (n == 255) return "OFF";
    if (n <= 0 || n > 127) return "---";
    static const char* names[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    return juce::String(names[n % 12]) + juce::String(n / 12 - 1);
}

// ---------------------------------------------------------------------------
// drawing helpers
// ---------------------------------------------------------------------------
namespace draw {

float textWidth(const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth(f, text);
}

void panel(juce::Graphics& g, juce::Rectangle<int> area, juce::Colour fill, juce::Colour border, float radius)
{
    const auto r = area.toFloat().reduced(0.5f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, radius);
    if (!border.isTransparent()) { g.setColour(border); g.drawRoundedRectangle(r, radius, 1.0f); }
}

void label(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification j, juce::Colour c, float px)
{
    g.setColour(c);
    g.setFont(Fonts::sans(px));
    g.drawText(text, area, j, true);
}

void heading(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    g.setColour(colours::textDim);
    g.setFont(Fonts::caption(11.0f, true));
    g.drawText(text.toUpperCase(), area, juce::Justification::centredLeft, true);
}

void caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification j, juce::Colour c, float px)
{
    g.setColour(c);
    g.setFont(Fonts::caption(px));
    g.drawText(text.toUpperCase(), area, j, true);
}

void switchTrack(juce::Graphics& g, juce::Rectangle<float> area, bool on, bool hover, bool enabled)
{
    using namespace colours;
    const auto track = area.withSizeKeepingCentre(30.0f, 16.0f).reduced(0.5f);
    const float radius = track.getHeight() * 0.5f;
    const float alpha = enabled ? 1.0f : 0.45f;
    g.setColour((on ? accentSoft : well).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, radius);
    g.setColour((on ? accent : (hover ? raisedHi : line)).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(track, radius, 1.0f);
    const float d = 10.0f;
    const float x = on ? track.getRight() - 2.5f - d : track.getX() + 2.5f;
    g.setColour((on ? accentHi : textDim).withMultipliedAlpha(alpha));
    g.fillEllipse(x, track.getCentreY() - d * 0.5f, d, d);
}

void dial(juce::Graphics& g, juce::Rectangle<float> area, float proportion, juce::Colour arc, juce::Colour pointer, bool enabled)
{
    using namespace colours;
    const float alpha = enabled ? 1.0f : 0.45f;
    const auto box = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()), juce::jmin(area.getWidth(), area.getHeight()));
    const float cx = box.getCentreX(), cy = box.getCentreY();
    const float outer = box.getWidth() * 0.5f;          // the arc ring sits 4 px outside the dial, 3 px thick
    const float ringR = outer - 1.5f;
    const float faceR = outer - 7.0f;
    const float startAngle = juce::degreesToRadians(-135.0f), sweep = juce::degreesToRadians(270.0f);
    const float p = juce::jlimit(0.0f, 1.0f, proportion);

    juce::Path track;
    track.addCentredArc(cx, cy, ringR, ringR, 0.0f, startAngle, startAngle + sweep, true);
    g.setColour(lineSoft.withMultipliedAlpha(alpha));
    g.strokePath(track, juce::PathStrokeType(3.0f));
    if (p > 0.001f) {
        juce::Path fill;
        fill.addCentredArc(cx, cy, ringR, ringR, 0.0f, startAngle, startAngle + sweep * p, true);
        g.setColour(arc.withMultipliedAlpha(alpha));
        g.strokePath(fill, juce::PathStrokeType(3.0f));
    }

    // the face: radial-gradient(circle at 40% 35%, #454a52, #22252a 70%)
    const float d = faceR * 2.0f;
    juce::ColourGradient grad(juce::Colour(0xff454a52), cx - 0.1f * d, cy - 0.15f * d,
                              juce::Colour(0xff22252a), cx - 0.1f * d, cy - 0.15f * d + 0.7f * d, true);
    g.setColour(juce::Colours::black.withAlpha(0.5f * alpha));
    g.fillEllipse(cx - faceR, cy - faceR + 2.0f, d, d);          // drop shadow
    g.setGradientFill(grad);
    g.setOpacity(alpha);
    g.fillEllipse(cx - faceR, cy - faceR, d, d);
    g.setOpacity(1.0f);
    g.setColour(juce::Colour(0xff111111).withMultipliedAlpha(alpha));
    g.drawEllipse(cx - faceR + 0.5f, cy - faceR + 0.5f, d - 1.0f, d - 1.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f * alpha));   // inset top highlight
    juce::Path hi;
    hi.addCentredArc(cx, cy, faceR - 1.5f, faceR - 1.5f, 0.0f, juce::degreesToRadians(-60.0f), juce::degreesToRadians(60.0f), true);
    g.strokePath(hi, juce::PathStrokeType(1.0f));

    // the pointer, on the rim so the value can sit in the middle
    const float a = startAngle + sweep * p;
    const float s = std::sin(a), c = std::cos(a);
    g.setColour(pointer.withMultipliedAlpha(alpha));
    g.drawLine(cx + s * (faceR - 6.0f), cy - c * (faceR - 6.0f), cx + s * (faceR - 1.5f), cy - c * (faceR - 1.5f), 2.0f);
}

} // namespace draw

// ---------------------------------------------------------------------------
// LookAndFeel
// ---------------------------------------------------------------------------
namespace {
constexpr float kRadius = 4.0f;

bool isPrimary(const juce::Button& b)
{
    return b.findColour(juce::TextButton::buttonColourId) == colours::accent;
}
bool isSmall(int height) { return height <= 20; }

juce::TextLayout tooltipLayout(const juce::String& text, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification(juce::Justification::centredLeft);
    s.append(text, Fonts::sans(12.0f), colour);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths(s, 320.0f);
    return tl;
}
} // namespace

ChipBoyLookAndFeel::ChipBoyLookAndFeel()
{
    using namespace colours;
    setColourScheme({ bg, panel, panel, line, text, accent, text, raised, textMute });
    if (auto face = faces().sans) setDefaultSansSerifTypeface(face);

    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::DocumentWindow::textColourId, text);
    setColour(juce::Label::textColourId, text);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textWhenEditingColourId, text);
    setColour(juce::Label::backgroundWhenEditingColourId, well);
    setColour(juce::Label::outlineWhenEditingColourId, accentHi);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::buttonOnColourId, accentSoft);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, text);
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, accentHi);
    setColour(juce::ToggleButton::tickDisabledColourId, textDim);
    setColour(juce::ComboBox::backgroundColourId, well);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::ComboBox::buttonColourId, well);
    setColour(juce::ComboBox::arrowColourId, textMute);
    setColour(juce::ComboBox::focusedOutlineColourId, accentHi);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, textMute);
    setColour(juce::PopupMenu::headerTextColourId, textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, raised);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::TextEditor::backgroundColourId, well);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::highlightColourId, accentSoft);
    setColour(juce::TextEditor::highlightedTextColourId, text);
    setColour(juce::TextEditor::outlineColourId, line);
    setColour(juce::TextEditor::focusedOutlineColourId, accentHi);
    setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId, accentHi);
    setColour(juce::Slider::backgroundColourId, well);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::thumbColourId, text);
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, lineSoft);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, well);
    setColour(juce::Slider::textBoxHighlightColourId, accentSoft);
    setColour(juce::Slider::textBoxOutlineColourId, line);
    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, raisedHi);
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour(juce::TooltipWindow::backgroundColourId, panel);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, line);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, lineSoft);
    setColour(juce::TabbedButtonBar::tabTextColourId, textMute);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, lineSoft);
    setColour(juce::TabbedButtonBar::frontTextColourId, text);
    setColour(juce::TabbedComponent::backgroundColourId, well);
    setColour(juce::TabbedComponent::outlineColourId, lineSoft);
    setColour(juce::ListBox::backgroundColourId, panel2);
    setColour(juce::ListBox::outlineColourId, lineSoft);
    setColour(juce::ListBox::textColourId, text);
    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::AlertWindow::outlineColourId, line);
    setColour(juce::GroupComponent::outlineColourId, lineSoft);
    setColour(juce::GroupComponent::textColourId, textDim);
    setColour(juce::HyperlinkButton::textColourId, accentHi);
    setColour(juce::ProgressBar::backgroundColourId, well);
    setColour(juce::ProgressBar::foregroundColourId, accent);
    setColour(juce::TableHeaderComponent::textColourId, textDim);
    setColour(juce::TableHeaderComponent::backgroundColourId, panel2);
    setColour(juce::TableHeaderComponent::outlineColourId, lineSoft);
    setColour(juce::TableHeaderComponent::highlightColourId, raised);
    setColour(juce::BubbleComponent::backgroundColourId, panel);
    setColour(juce::BubbleComponent::outlineColourId, line);
    setColour(juce::PropertyComponent::backgroundColourId, panel2);
    setColour(juce::PropertyComponent::labelTextColourId, textMute);
    setColour(juce::DrawableButton::textColourId, text);
    setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::DrawableButton::backgroundOnColourId, accentSoft);
}

ChipBoyLookAndFeel::~ChipBoyLookAndFeel() = default;

juce::Typeface::Ptr ChipBoyLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    const auto& f = faces();
    const auto name = font.getTypefaceName();
    if (name == juce::Font::getDefaultMonospacedFontName() && f.mono != nullptr) return f.mono;
    if (name == juce::Font::getDefaultSansSerifFontName() && f.sans != nullptr)
        return font.isBold() && f.sansSemi != nullptr ? f.sansSemi : f.sans;
    return juce::LookAndFeel_V4::getTypefaceForFont(font);
}

// --- buttons: .btn, .btn.primary, .btn.small, .btn[aria-pressed] ----------
juce::Font ChipBoyLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return Fonts::sans(isSmall(buttonHeight) ? 11.0f : 12.0f);
}

int ChipBoyLookAndFeel::getTextButtonWidthToFitText(juce::TextButton& b, int buttonHeight)
{
    return juce::roundToInt(draw::textWidth(getTextButtonFont(b, buttonHeight), b.getButtonText())) + (isSmall(buttonHeight) ? 16 : 22);
}

void ChipBoyLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour, bool over, bool down)
{
    using namespace colours;
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    const bool custom = backgroundColour != raised && backgroundColour != accent && backgroundColour != accentSoft;
    juce::Colour fill, border;
    if (isPrimary(b))       { fill = down ? accent.darker(0.15f) : over ? accentHi : accent; border = juce::Colours::transparentBlack; }
    else if (on)            { fill = accentSoft; border = accent; }
    else if (custom)        { fill = down ? backgroundColour.darker(0.2f) : over ? backgroundColour.brighter(0.15f) : backgroundColour; border = line; }
    else                    { fill = down ? raised.darker(0.2f) : over ? raisedHi : raised; border = line; }
    const float alpha = b.isEnabled() ? 1.0f : 0.45f;
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, kRadius);
    if (!border.isTransparent()) { g.setColour(border.withMultipliedAlpha(alpha)); g.drawRoundedRectangle(r, kRadius, 1.0f); }
    if (b.hasKeyboardFocus(false) && !down) { g.setColour(accentHi.withAlpha(0.7f)); g.drawRoundedRectangle(r.expanded(1.0f), kRadius + 1.0f, 1.0f); }
}

void ChipBoyLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight()));
    juce::Colour c = isPrimary(b) ? juce::Colours::white
                                  : b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (!b.isEnabled()) c = c.withMultipliedAlpha(0.45f);
    g.setColour(c);
    g.drawText(b.getButtonText(), b.getLocalBounds().reduced(isSmall(b.getHeight()) ? 4 : 6, 0), juce::Justification::centred, false);
}

// --- .switch --------------------------------------------------------------
void ChipBoyLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    draw::switchTrack(g, r.removeFromLeft(30.0f), b.getToggleState(), over, b.isEnabled());
    g.setColour(b.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.45f));
    g.setFont(Fonts::sans(12.0f));
    g.drawText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft(38), juce::Justification::centredLeft, true);
    if (b.hasKeyboardFocus(false)) { g.setColour(colours::accentHi.withAlpha(0.7f)); g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(0.5f), kRadius, 1.0f); }
}

// --- .sel -----------------------------------------------------------------
void ChipBoyLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int bx, int by, int bw, int bh, juce::ComboBox& box)
{
    using namespace colours;
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, float(width), float(height)).reduced(0.5f);
    const float alpha = box.isEnabled() ? 1.0f : 0.45f;
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, kRadius);
    const bool hover = isButtonDown || box.isMouseOver(true) || box.isPopupActive();
    g.setColour((box.hasKeyboardFocus(true) ? box.findColour(juce::ComboBox::focusedOutlineColourId)
                                             : hover ? raisedHi : box.findColour(juce::ComboBox::outlineColourId)).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, kRadius, 1.0f);
    const float cx = float(bx) + float(bw) * 0.5f, cy = float(by) + float(bh) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(alpha));
    g.strokePath(chevron, juce::PathStrokeType(1.5f));
}

juce::Font ChipBoyLookAndFeel::getComboBoxFont(juce::ComboBox&) { return Fonts::sans(12.0f); }

void ChipBoyLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(1, 1, juce::jmax(10, box.getWidth() - box.getHeight() - 2), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

// --- popup menus ----------------------------------------------------------
void ChipBoyLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(colours::line);
    g.drawRect(0, 0, width, height, 1);
}

juce::Font ChipBoyLookAndFeel::getPopupMenuFont() { return Fonts::sans(12.0f); }

void ChipBoyLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 50; idealHeight = 9; return; }
    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 24;
    idealWidth = juce::roundToInt(draw::textWidth(getPopupMenuFont(), text)) + 46;
}

int ChipBoyLookAndFeel::getPopupMenuBorderSize() { return 4; }

void ChipBoyLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                           const juce::Drawable* icon, const juce::Colour* textColour)
{
    using namespace colours;
    if (isSeparator) {
        g.setColour(lineSoft);
        g.fillRect(area.reduced(6, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }
    auto r = area.reduced(3, 0);
    if (isHighlighted && isActive) { g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId)); g.fillRoundedRectangle(r.toFloat(), 3.0f); }
    juce::Colour c = textColour != nullptr ? *textColour
                   : isHighlighted ? findColour(juce::PopupMenu::highlightedTextColourId) : findColour(juce::PopupMenu::textColourId);
    if (!isActive) c = textDim.withAlpha(0.7f);
    auto inner = r.reduced(8, 0);
    if (isTicked) { g.setColour(accentHi); g.fillEllipse(float(inner.getX()), float(inner.getCentreY()) - 2.5f, 5.0f, 5.0f); }
    if (icon != nullptr) icon->drawWithin(g, inner.removeFromLeft(inner.getHeight()).reduced(3).toFloat(), juce::RectanglePlacement::centred, isActive ? 1.0f : 0.5f);
    inner.removeFromLeft(14);
    if (hasSubMenu) {
        const float cx = float(inner.getRight()) - 4.0f, cy = float(inner.getCentreY());
        juce::Path p; p.startNewSubPath(cx - 2.0f, cy - 3.5f); p.lineTo(cx + 1.5f, cy); p.lineTo(cx - 2.0f, cy + 3.5f);
        g.setColour(c); g.strokePath(p, juce::PathStrokeType(1.5f));
        inner.removeFromRight(12);
    }
    if (shortcutKeyText.isNotEmpty()) {
        g.setColour(textDim); g.setFont(Fonts::mono(10.5f));
        g.drawText(shortcutKeyText, inner, juce::Justification::centredRight, true);
        inner.removeFromRight(juce::roundToInt(draw::textWidth(Fonts::mono(10.5f), shortcutKeyText)) + 12);
    }
    g.setColour(c);
    g.setFont(getPopupMenuFont());
    g.drawText(text, inner, juce::Justification::centredLeft, true);
}

void ChipBoyLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    draw::caption(g, sectionName, area.reduced(11, 0), juce::Justification::centredLeft, findColour(juce::PopupMenu::headerTextColourId));
}

// --- sliders --------------------------------------------------------------
int ChipBoyLookAndFeel::getSliderThumbRadius(juce::Slider&) { return 7; }

void ChipBoyLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                          juce::Slider::SliderStyle, juce::Slider& s)
{
    using namespace colours;
    const bool vertical = s.isVertical();
    const float alpha = s.isEnabled() ? 1.0f : 0.45f;
    const auto track = vertical ? juce::Rectangle<float>(float(x) + float(width) * 0.5f - 3.0f, float(y), 6.0f, float(height))
                                : juce::Rectangle<float>(float(x), float(y) + float(height) * 0.5f - 3.0f, float(width), 6.0f);
    g.setColour(s.findColour(juce::Slider::backgroundColourId).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, 3.0f);
    g.setColour(line.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(track.reduced(0.5f), 3.0f, 1.0f);
    const auto fill = vertical ? track.withTop(sliderPos) : track.withRight(sliderPos);
    if (fill.getWidth() > 2.0f && fill.getHeight() > 2.0f) {
        g.setColour(s.findColour(juce::Slider::trackColourId).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(fill.reduced(1.0f), 2.0f);
    }
    const auto thumb = vertical ? juce::Rectangle<float>(track.getCentreX() - 8.0f, sliderPos - 4.0f, 16.0f, 8.0f)
                                : juce::Rectangle<float>(sliderPos - 4.0f, track.getCentreY() - 8.0f, 8.0f, 16.0f);
    g.setColour((s.isMouseOverOrDragging() && s.isEnabled() ? accentHi : s.findColour(juce::Slider::thumbColourId)).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(thumb, 2.0f);
    g.setColour(juce::Colour(0xff111111).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(thumb.reduced(0.5f), 2.0f, 1.0f);
}

void ChipBoyLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float, float, juce::Slider& s)
{
    draw::dial(g, juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f), pos,
               s.findColour(juce::Slider::rotarySliderFillColourId), s.findColour(juce::Slider::thumbColourId), s.isEnabled());
}

// --- scrollbars, thin -----------------------------------------------------
int ChipBoyLookAndFeel::getDefaultScrollbarWidth() { return 8; }
int ChipBoyLookAndFeel::getMinimumScrollbarThumbSize(juce::ScrollBar&) { return 18; }
int ChipBoyLookAndFeel::getScrollbarButtonSize(juce::ScrollBar&) { return 0; }

void ChipBoyLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& sb, int x, int y, int width, int height, bool vertical,
                                       int thumbStart, int thumbSize, bool over, bool down)
{
    g.setColour(sb.findColour(juce::ScrollBar::backgroundColourId));
    g.fillRect(x, y, width, height);
    if (thumbSize <= 0) return;
    const float thickness = juce::jmin(4.0f, float(vertical ? width : height));
    const auto thumb = vertical ? juce::Rectangle<float>(float(x) + (float(width) - thickness) * 0.5f, float(thumbStart), thickness, float(thumbSize))
                                : juce::Rectangle<float>(float(thumbStart), float(y) + (float(height) - thickness) * 0.5f, float(thumbSize), thickness);
    g.setColour(over || down ? colours::textDim : sb.findColour(juce::ScrollBar::thumbColourId));
    g.fillRoundedRectangle(thumb, thickness * 0.5f);
}

// --- tooltips -------------------------------------------------------------
juce::Rectangle<int> ChipBoyLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto tl = tooltipLayout(tipText, colours::text);
    const int w = int(std::ceil(tl.getWidth())) + 18, h = int(std::ceil(tl.getHeight())) + 12;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

void ChipBoyLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, float(width), float(height));
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(r.reduced(0.5f), kRadius);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(r.reduced(0.5f), kRadius, 1.0f);
    tooltipLayout(text, findColour(juce::TooltipWindow::textColourId)).draw(g, r.reduced(9.0f, 6.0f));
}

// --- text editors: .name-input --------------------------------------------
void ChipBoyLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& te)
{
    g.setColour(te.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, float(width), float(height)), kRadius);
}

void ChipBoyLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& te)
{
    if (!te.isEnabled()) return;
    const auto c = te.hasKeyboardFocus(true) && !te.isReadOnly() ? te.findColour(juce::TextEditor::focusedOutlineColourId)
                                                                  : te.findColour(juce::TextEditor::outlineColourId);
    if (c.isTransparent()) return;
    g.setColour(c);
    g.drawRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, float(width), float(height)).reduced(0.5f), kRadius, 1.0f);
}

// --- tabs: .tabs button ---------------------------------------------------
void ChipBoyLookAndFeel::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    using namespace colours;
    const auto r = b.getActiveArea().toFloat();
    const bool front = b.isFrontTab();
    if (front) {
        juce::Path p;
        p.addRoundedRectangle(r.getX() + 0.5f, r.getY() + 0.5f, r.getWidth() - 1.0f, r.getHeight() + 8.0f, 5.0f, 5.0f, true, true, false, false);
        g.setColour(well); g.fillPath(p);
        g.setColour(b.findColour(juce::TabbedButtonBar::frontOutlineColourId)); g.strokePath(p, juce::PathStrokeType(1.0f));
    }
    g.setColour(front ? b.findColour(juce::TabbedButtonBar::frontTextColourId) : over ? text : b.findColour(juce::TabbedButtonBar::tabTextColourId));
    g.setFont(Fonts::sans(12.5f));
    g.drawText(b.getButtonText(), r.toNearestInt(), juce::Justification::centred, false);
}

int ChipBoyLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& b, int)
{
    return juce::roundToInt(draw::textWidth(Fonts::sans(12.5f), b.getButtonText())) + 26;
}

int ChipBoyLookAndFeel::getTabButtonOverlap(int) { return -2; }
void ChipBoyLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}
void ChipBoyLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) {}

// --- windows --------------------------------------------------------------
void ChipBoyLookAndFeel::drawDocumentWindowTitleBar(juce::DocumentWindow& w, juce::Graphics& g, int width, int height, int titleSpaceX, int titleSpaceW,
                                                    const juce::Image* icon, bool drawTitleTextOnLeft)
{
    using namespace colours;
    g.fillAll(panel2);
    g.setColour(lineSoft);
    g.fillRect(0, height - 1, width, 1);
    auto textArea = juce::Rectangle<int>(titleSpaceX, 0, titleSpaceW, height);
    if (icon != nullptr) {
        g.drawImageWithin(*icon, textArea.getX(), 4, height - 8, height - 8, juce::RectanglePlacement::centred);
        textArea.removeFromLeft(height);
    }
    g.setColour(w.findColour(juce::DocumentWindow::textColourId).withMultipliedAlpha(w.isActiveWindow() ? 1.0f : 0.6f));
    g.setFont(Fonts::sans(13.0f, true));
    g.drawText(w.getName(), textArea, drawTitleTextOnLeft ? juce::Justification::centredLeft : juce::Justification::centred, true);
}

juce::Font ChipBoyLookAndFeel::getAlertWindowTitleFont()   { return Fonts::sans(15.0f, true); }
juce::Font ChipBoyLookAndFeel::getAlertWindowMessageFont() { return Fonts::sans(13.0f); }
juce::Font ChipBoyLookAndFeel::getAlertWindowFont()        { return Fonts::sans(12.0f); }

} // namespace chipboy::ui
