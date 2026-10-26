#include "Theme.h"
#include "BinaryData.h"

namespace hvx::ui
{
namespace
{
juce::Typeface::Ptr face (const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor (data, size);
}

struct Faces
{
    juce::Typeface::Ptr regular  = face (BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
    juce::Typeface::Ptr medium   = face (BinaryData::BarlowMedium_ttf, BinaryData::BarlowMedium_ttfSize);
    juce::Typeface::Ptr semibold = face (BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize);
    juce::Typeface::Ptr boldF    = face (BinaryData::BarlowBold_ttf, BinaryData::BarlowBold_ttfSize);
    juce::Typeface::Ptr condSemi = face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);
    juce::Typeface::Ptr condBold = face (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
    juce::Typeface::Ptr monoF    = face (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);

    static Faces& get() { static Faces f; return f; }
};

juce::Font make (juce::Typeface::Ptr t, float h)
{
    return juce::Font (juce::FontOptions (t).withHeight (h));
}
} // namespace

juce::Font Fonts::body (float h)        { return make (Faces::get().regular, h); }
juce::Font Fonts::medium (float h)      { return make (Faces::get().medium, h); }
juce::Font Fonts::semi (float h)        { return make (Faces::get().semibold, h); }
juce::Font Fonts::bold (float h)        { return make (Faces::get().boldF, h); }
juce::Font Fonts::display (float h)     { return make (Faces::get().condBold, h); }
juce::Font Fonts::displaySemi (float h) { return make (Faces::get().condSemi, h); }
juce::Font Fonts::mono (float h)        { return make (Faces::get().monoF, h); }
juce::Typeface::Ptr Fonts::regularFace() { return Faces::get().regular; }

juce::ColourGradient accentGradient (juce::Rectangle<float> r, float alpha)
{
    return juce::ColourGradient (col::ember.withMultipliedAlpha (alpha), r.getX(), r.getCentreY(),
                                 col::flame.withMultipliedAlpha (alpha), r.getRight(), r.getCentreY(), false);
}

void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c)
{
    g.setColour (c);
    auto f = Fonts::semi (11.0f).withExtraKerningFactor (0.12f);
    g.setFont (f);
    g.drawText (text.toUpperCase(), r, juce::Justification::centredLeft, false);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::PopupMenu::backgroundColourId, col::raised);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::raisedHi);
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::raised);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::border);
    setColour (juce::ComboBox::arrowColourId, col::textDim);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::ember.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, col::ember);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::ScrollBar::thumbColourId, col::borderHi);
    setColour (juce::TooltipWindow::backgroundColourId, col::raisedHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::ListBox::backgroundColourId, col::panel);
    setDefaultSansSerifTypeface (Fonts::regularFace());
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return Fonts::regularFace();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

//==============================================================================
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                     float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const auto c = r.getCentre();
    const float radius = d * 0.5f;
    const bool enabled = s.isEnabled();
    const bool bipolar = s.getProperties().contains ("bipolar");

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0, start, end, true);
    g.setColour (col::border);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float angle = start + pos * (end - start);
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
        if (enabled) g.setGradientFill (accentGradient (r));
        else g.setColour (col::textFaint);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    auto body = r.reduced (radius * 0.28f);
    g.setColour (col::raised);
    g.fillEllipse (body);
    g.setColour (col::borderHi.withAlpha (0.6f));
    g.drawEllipse (body, 1.0f);

    // pointer
    const float pr = body.getWidth() * 0.5f;
    juce::Point<float> tip (c.x + std::sin (angle) * (pr - 3.0f), c.y - std::cos (angle) * (pr - 3.0f));
    juce::Point<float> base (c.x + std::sin (angle) * (pr * 0.35f), c.y - std::cos (angle) * (pr * 0.35f));
    g.setColour (enabled ? col::text : col::textFaint);
    g.drawLine ({ base, tip }, 2.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                     juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto track = r.withSizeKeepingCentre (r.getWidth(), 3.0f);
    g.setColour (col::border);
    g.fillRoundedRectangle (track, 1.5f);
    const bool bipolar = s.getProperties().contains ("bipolar");
    const float cx = bipolar ? r.getCentreX() : r.getX();
    auto fill = juce::Rectangle<float> (juce::jmin (cx, pos), track.getY(), std::abs (pos - cx), 3.0f);
    g.setGradientFill (accentGradient (r));
    g.fillRoundedRectangle (fill, 1.5f);
    g.setColour (col::text);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ pos, r.getCentreY() }));
}

//==============================================================================
void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto kind = b.getProperties()["kind"].toString();
    const float rad = kind == "chip" ? r.getHeight() * 0.5f : 7.0f;
    const bool on = b.getToggleState();
    const float a = b.isEnabled() ? 1.0f : 0.45f;

    if (kind == "primary")
    {
        g.setGradientFill (accentGradient (r, a));
        g.fillRoundedRectangle (r, rad);
        if (over) { g.setColour (juce::Colours::white.withAlpha (0.10f)); g.fillRoundedRectangle (r, rad); }
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r, rad); }
        return;
    }
    if (kind == "ghost")
    {
        if (over || on) { g.setColour (col::raised.withMultipliedAlpha (a)); g.fillRoundedRectangle (r, rad); }
        return;
    }
    if (kind == "seg" || kind == "chip" || kind == "tab")
    {
        if (on)
        {
            g.setColour (col::ember.withAlpha (0.14f * a));
            g.fillRoundedRectangle (r, rad);
            g.setGradientFill (accentGradient (r, a));
            g.drawRoundedRectangle (r, rad, 1.2f);
        }
        else
        {
            g.setColour ((over ? col::raisedHi : col::raised).withMultipliedAlpha (a));
            g.fillRoundedRectangle (r, rad);
            g.setColour (col::border);
            g.drawRoundedRectangle (r, rad, 1.0f);
        }
        return;
    }
    // secondary
    g.setColour ((down ? col::raisedHi.brighter (0.05f) : over ? col::raisedHi : col::raised).withMultipliedAlpha (a));
    g.fillRoundedRectangle (r, rad);
    g.setColour (on ? col::ember.withAlpha (0.8f) : col::border);
    g.drawRoundedRectangle (r, rad, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    const auto kind = b.getProperties()["kind"].toString();
    if (kind == "chip") return Fonts::semi (juce::jmin (12.0f, h * 0.55f));
    return Fonts::semi (juce::jmin (13.5f, h * 0.5f));
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto kind = b.getProperties()["kind"].toString();
    juce::Colour c = col::text;
    if (kind == "primary") c = juce::Colour (0xff1a0b0b);
    else if (kind == "ghost") c = b.getToggleState() ? col::text : col::textDim;
    else if ((kind == "seg" || kind == "chip") && ! b.getToggleState()) c = col::textDim.brighter (0.15f);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
    g.setColour (c);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.85f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    // pill switch on the right, label on the left
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromRight (38.0f).withSizeKeepingCentre (36.0f, 20.0f);
    const bool on = b.getToggleState();
    if (on) g.setGradientFill (accentGradient (sw, b.isEnabled() ? 1.0f : 0.4f));
    else g.setColour (over ? col::raisedHi : col::raised);
    g.fillRoundedRectangle (sw, 10.0f);
    if (! on) { g.setColour (col::borderHi); g.drawRoundedRectangle (sw.reduced (0.5f), 10.0f, 1.0f); }
    auto knob = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ on ? sw.getRight() - 10.0f : sw.getX() + 10.0f, sw.getCentreY() });
    g.setColour (on ? juce::Colour (0xff1a0b0b) : col::textDim);
    g.fillEllipse (knob);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.isEnabled() ? col::text : col::textFaint);
        g.setFont (Fonts::medium (13.5f));
        g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedRight (6), juce::Justification::centredLeft, 2);
    }
}

//==============================================================================
void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isPopupActive() ? col::raisedHi : col::raised);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::borderHi : col::border);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = h * 0.5f;
    arrow.startNewSubPath (ax - 4.5f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.5f, ay - 2.0f);
    g.setColour (col::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return Fonts::medium (13.5f); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::raised);
    g.setColour (col::borderHi);
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                      bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                      const juce::String& shortcut, const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (col::border);
        g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (col::raisedHi);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
    }
    if (isTicked)
    {
        g.setGradientFill (accentGradient (r.toFloat()));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 11.0f, (float) r.getCentreY() }));
    }
    g.setColour (textColour != nullptr ? *textColour : (isActive ? col::text : col::textFaint));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, r.withTrimmedLeft (22).withTrimmedRight (hasSubMenu ? 20 : 8), juce::Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
    {
        g.setColour (col::textFaint);
        g.setFont (Fonts::mono (12.0f));
        g.drawText (shortcut, r.withTrimmedRight (10), juce::Justification::centredRight);
    }
    if (hasSubMenu)
    {
        juce::Path p;
        const float x = (float) r.getRight() - 12.0f, y = (float) r.getCentreY();
        p.startNewSubPath (x - 2, y - 4); p.lineTo (x + 2, y); p.lineTo (x - 2, y + 4);
        g.setColour (col::textDim);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}

juce::Font LookAndFeel::getPopupMenuFont() { return Fonts::medium (14.0f); }

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 9; return; }
    h = 28;
    w = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 60;
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                  int thumbStart, int thumbSize, bool over, bool down)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 3, (float) thumbStart, (float) w - 6, (float) thumbSize)
                                            : juce::Rectangle<float> ((float) thumbStart, (float) y + 3, (float) thumbSize, (float) h - 6);
    g.setColour ((over || down) ? col::borderHi.brighter (0.2f) : col::borderHi);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (col::raisedHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::borderHi);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (col::text);
    g.setFont (Fonts::body (13.0f));
    g.drawFittedText (text, r.toNearestInt().reduced (10, 6), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (320, juce::GlyphArrangement::getStringWidthInt (Fonts::body (13.0f), text) + 24);
    const int lines = juce::jmax (1, (juce::GlyphArrangement::getStringWidthInt (Fonts::body (13.0f), text) + 24) / 320 + 1);
    const int h = 14 + lines * 17;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (parent);
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::raised);
    g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h), 7.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? col::ember.withAlpha (0.7f) : col::border);
    g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 7.0f, 1.0f);
}

void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (col::raised);
    g.fillPath (path);
    g.setColour (col::borderHi);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}
} // namespace hvx::ui
