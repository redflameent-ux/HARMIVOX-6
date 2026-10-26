#include "Widgets.h"

namespace hvx::ui
{
//==============================================================================
void Icons::draw (juce::Graphics& g, Id id, juce::Rectangle<float> r, juce::Colour c, float stroke)
{
    // icons are drawn on a 24x24 grid
    const float s = juce::jmin (r.getWidth(), r.getHeight()) / 24.0f;
    const auto o = r.getCentre() - juce::Point<float> (12.0f * s, 12.0f * s);
    auto P = [&] (float x, float y) { return juce::Point<float> (o.x + x * s, o.y + y * s); };
    juce::Path p;
    bool fill = false;
    auto line = [&] (float x1, float y1, float x2, float y2) { p.startNewSubPath (P (x1, y1)); p.lineTo (P (x2, y2)); };

    switch (id)
    {
        case Id::speaker:
        case Id::speakerOff:
            p.startNewSubPath (P (4, 9.5f)); p.lineTo (P (8, 9.5f)); p.lineTo (P (12.5f, 5.5f)); p.lineTo (P (12.5f, 18.5f));
            p.lineTo (P (8, 14.5f)); p.lineTo (P (4, 14.5f)); p.closeSubPath();
            if (id == Id::speaker) { p.startNewSubPath (P (16, 9)); p.quadraticTo (P (18, 12), P (16, 15)); p.startNewSubPath (P (18.5f, 6.5f)); p.quadraticTo (P (22, 12), P (18.5f, 17.5f)); }
            else { line (16, 9.5f, 21, 14.5f); line (21, 9.5f, 16, 14.5f); }
            break;
        case Id::headphones:
            p.startNewSubPath (P (4, 15)); p.lineTo (P (4, 12)); p.cubicTo (P (4, 6), P (20, 6), P (20, 12)); p.lineTo (P (20, 15));
            p.addRoundedRectangle (juce::Rectangle<float> (P (3.5f, 13.5f), P (8, 19.5f)), 1.5f * s);
            p.addRoundedRectangle (juce::Rectangle<float> (P (16, 13.5f), P (20.5f, 19.5f)), 1.5f * s);
            break;
        case Id::pencil:
            p.startNewSubPath (P (5, 19)); p.lineTo (P (6, 15)); p.lineTo (P (15.5f, 5.5f)); p.lineTo (P (18.5f, 8.5f));
            p.lineTo (P (9, 18)); p.closeSubPath(); line (13.5f, 7.5f, 16.5f, 10.5f);
            break;
        case Id::dots:
            for (int i = 0; i < 3; ++i) p.addEllipse (juce::Rectangle<float> (2.6f * s, 2.6f * s).withCentre (P (6.5f + i * 5.5f, 12)));
            fill = true; break;
        case Id::pointer:
            p.startNewSubPath (P (7, 4)); p.lineTo (P (7, 19)); p.lineTo (P (10.5f, 15.5f)); p.lineTo (P (13, 21));
            p.lineTo (P (15.5f, 20)); p.lineTo (P (13, 14.5f)); p.lineTo (P (18, 14.5f)); p.closeSubPath();
            break;
        case Id::move:
            line (12, 3, 12, 21); line (3, 12, 21, 12);
            p.startNewSubPath (P (9, 6)); p.lineTo (P (12, 3)); p.lineTo (P (15, 6));
            p.startNewSubPath (P (9, 18)); p.lineTo (P (12, 21)); p.lineTo (P (15, 18));
            p.startNewSubPath (P (6, 9)); p.lineTo (P (3, 12)); p.lineTo (P (6, 15));
            p.startNewSubPath (P (18, 9)); p.lineTo (P (21, 12)); p.lineTo (P (18, 15));
            break;
        case Id::scissors:
            p.addEllipse (juce::Rectangle<float> (P (4, 14), P (10, 20)));
            p.addEllipse (juce::Rectangle<float> (P (14, 14), P (20, 20)));
            line (9, 15, 18, 4); line (15, 15, 6, 4);
            break;
        case Id::marker:
            p.startNewSubPath (P (7, 4)); p.lineTo (P (17, 4)); p.lineTo (P (12, 10)); p.closeSubPath();
            line (12, 10, 12, 21);
            break;
        case Id::mic:
            p.addRoundedRectangle (juce::Rectangle<float> (P (9, 3), P (15, 14)), 3.0f * s);
            p.startNewSubPath (P (6, 11)); p.cubicTo (P (6, 19), P (18, 19), P (18, 11));
            line (12, 17, 12, 21); line (9, 21, 15, 21);
            break;
        case Id::play:
            p.startNewSubPath (P (8, 5)); p.lineTo (P (19, 12)); p.lineTo (P (8, 19)); p.closeSubPath(); fill = true; break;
        case Id::stop:
            p.addRoundedRectangle (juce::Rectangle<float> (P (7, 7), P (17, 17)), 1.5f * s); fill = true; break;
        case Id::record:
            p.addEllipse (juce::Rectangle<float> (P (6, 6), P (18, 18))); fill = true; break;
        case Id::help:
            p.startNewSubPath (P (9, 9)); p.cubicTo (P (9, 5.5f), P (15, 5.5f), P (15, 9)); p.cubicTo (P (15, 11.5f), P (12, 11.5f), P (12, 14));
            p.addEllipse (juce::Rectangle<float> (2.2f * s, 2.2f * s).withCentre (P (12, 17.5f)));
            break;
        case Id::plus:  line (12, 5, 12, 19); line (5, 12, 19, 12); break;
        case Id::minus: line (5, 12, 19, 12); break;
        case Id::grip:
            for (int yy = 0; yy < 3; ++yy) for (int xx = 0; xx < 2; ++xx)
                p.addEllipse (juce::Rectangle<float> (2.4f * s, 2.4f * s).withCentre (P (9.5f + xx * 5, 6.5f + yy * 5.5f)));
            fill = true; break;
        case Id::chevronDown:  p.startNewSubPath (P (7, 10)); p.lineTo (P (12, 15)); p.lineTo (P (17, 10)); break;
        case Id::chevronLeft:  p.startNewSubPath (P (14, 6)); p.lineTo (P (8, 12)); p.lineTo (P (14, 18)); break;
        case Id::chevronRight: p.startNewSubPath (P (10, 6)); p.lineTo (P (16, 12)); p.lineTo (P (10, 18)); break;
        case Id::sparkle:
            p.startNewSubPath (P (12, 3)); p.quadraticTo (P (13, 11), P (21, 12)); p.quadraticTo (P (13, 13), P (12, 21));
            p.quadraticTo (P (11, 13), P (3, 12)); p.quadraticTo (P (11, 11), P (12, 3)); fill = true; break;
        case Id::list:
            for (int i = 0; i < 3; ++i) line (5, 7 + i * 5.0f, 19, 7 + i * 5.0f);
            break;
        case Id::notes:
            line (4, 8, 11, 8); line (9, 13, 17, 13); line (13, 18, 20, 18);
            break;
        case Id::check: p.startNewSubPath (P (5, 12.5f)); p.lineTo (P (10, 17)); p.lineTo (P (19, 7)); break;
        case Id::cross: line (6, 6, 18, 18); line (18, 6, 6, 18); break;
        case Id::folder:
            p.startNewSubPath (P (3, 7)); p.lineTo (P (9, 7)); p.lineTo (P (11, 9)); p.lineTo (P (21, 9)); p.lineTo (P (21, 19));
            p.lineTo (P (3, 19)); p.closeSubPath(); break;
        case Id::sync:
            p.addCentredArc (o.x + 12 * s, o.y + 12 * s, 7 * s, 7 * s, 0, 0.3f, 2.9f, true);
            p.addCentredArc (o.x + 12 * s, o.y + 12 * s, 7 * s, 7 * s, 0, 3.4f, 6.0f, true);
            break;
        case Id::history:
            p.addCentredArc (o.x + 12 * s, o.y + 12 * s, 8 * s, 8 * s, 0, -2.4f, 3.5f, true);
            line (12, 8, 12, 12); line (12, 12, 15, 14);
            break;
        case Id::undo:
            p.startNewSubPath (P (9, 6)); p.lineTo (P (5, 10)); p.lineTo (P (9, 14));
            p.startNewSubPath (P (5, 10)); p.lineTo (P (15, 10)); p.cubicTo (P (21, 10), P (21, 19), P (15, 19)); p.lineTo (P (11, 19));
            break;
        case Id::redo:
            p.startNewSubPath (P (15, 6)); p.lineTo (P (19, 10)); p.lineTo (P (15, 14));
            p.startNewSubPath (P (19, 10)); p.lineTo (P (9, 10)); p.cubicTo (P (3, 10), P (3, 19), P (9, 19)); p.lineTo (P (13, 19));
            break;
        case Id::magnet:
            p.startNewSubPath (P (6, 4)); p.lineTo (P (6, 12)); p.cubicTo (P (6, 20), P (18, 20), P (18, 12)); p.lineTo (P (18, 4));
            line (6, 7.5f, 9.5f, 7.5f); line (14.5f, 7.5f, 18, 7.5f);
            p.startNewSubPath (P (9.5f, 4)); p.lineTo (P (9.5f, 12)); p.cubicTo (P (9.5f, 15.5f), P (14.5f, 15.5f), P (14.5f, 12)); p.lineTo (P (14.5f, 4));
            break;
    }
    g.setColour (c);
    if (fill) g.fillPath (p);
    else g.strokePath (p, juce::PathStrokeType (stroke * juce::jmax (0.7f, s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
Knob::Knob (const juce::String& l, double min, double max, double dflt, std::function<juce::String (double)> fmt, double skewMid)
    : label (l), format (std::move (fmt))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (min, max, 0.0);
    if (skewMid > min && skewMid < max) slider.setSkewFactorFromMidPoint (skewMid);
    slider.setValue (dflt, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, dflt);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    slider.onValueChange = [this] { repaint(); if (onChange) onChange (slider.getValue()); };
    slider.onDragStart = [this] { if (onDragStart) onDragStart(); };
    slider.setPopupMenuEnabled (false);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (18);
    const int d = juce::jmin (r.getWidth(), r.getHeight(), 44);
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (isEnabled() ? col::textDim : col::textFaint);
    g.setFont (Fonts::medium (12.5f));
    g.drawFittedText (label, r.removeFromTop (16), juce::Justification::centred, 1, 0.8f);
    g.setColour (isEnabled() ? col::text : col::textFaint);
    g.setFont (Fonts::semi (13.0f));
    const auto v = slider.getValue();
    g.drawFittedText (format ? format (v) : juce::String (v, 1), r.removeFromBottom (17), juce::Justification::centred, 1);
}

//==============================================================================
IconButton::IconButton (Icons::Id i, const juce::String& tip, bool toggle)
    : juce::Button (tip), icon (i)
{
    setTooltip (tip);
    setClickingTogglesState (toggle);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    if (framed || over || on)
    {
        g.setColour (on ? onColour.withAlpha (0.16f) : (over ? col::raisedHi : col::raised));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (on ? onColour.withAlpha (0.85f) : col::border);
        if (framed || on) g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.15f)); g.fillRoundedRectangle (r, 6.0f); }

    const juce::Colour c = ! isEnabled() ? col::textFaint : (on ? col::text : (over ? col::text : col::textDim));
    if (text.isEmpty())
    {
        const float sz = juce::jmin (r.getWidth(), r.getHeight()) * 0.62f;
        Icons::draw (g, icon, r.withSizeKeepingCentre (sz, sz), c);
    }
    else
    {
        auto ir = r.reduced (10.0f, 0);
        const float sz = juce::jmin (18.0f, r.getHeight() * 0.6f);
        const float textW = juce::GlyphArrangement::getStringWidth (Fonts::semi (13.5f), text);
        const float total = sz + 8.0f + textW;
        auto content = ir.withSizeKeepingCentre (juce::jmin (total, ir.getWidth()), r.getHeight());
        Icons::draw (g, icon, content.removeFromLeft (sz).withSizeKeepingCentre (sz, sz), c);
        content.removeFromLeft (8.0f);
        g.setColour (c);
        g.setFont (Fonts::semi (13.5f));
        g.drawText (text, content, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void setupCombo (juce::ComboBox& c)
{
    c.setJustificationType (juce::Justification::centredLeft);
}

void PanelTitle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto help = r.removeFromRight (30).withSizeKeepingCentre (24, 24).toFloat();
    g.setColour (col::text);
    g.setFont (Fonts::display (24.0f));
    g.drawText (title, r.removeFromTop (28), juce::Justification::bottomLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (13.5f));
    g.drawText (subtitle, r.removeFromTop (20), juce::Justification::centredLeft);
    if (onHelp)
    {
        help = help.withY (6.0f);
        g.setColour (col::borderHi);
        g.drawEllipse (help.reduced (1.0f), 1.2f);
        Icons::draw (g, Icons::Id::help, help.reduced (5.0f), col::textDim, 1.8f);
    }
}

void PanelTitle::mouseUp (const juce::MouseEvent& e)
{
    if (onHelp && e.x > getWidth() - 34 && e.y < 34) onHelp();
}

//==============================================================================
void drawWaveform (juce::Graphics& g, const juce::AudioBuffer<float>& b, juce::Rectangle<float> r,
                   double startSample, double spp, juce::Colour c)
{
    const int n = b.getNumSamples();
    if (n == 0 || spp <= 0) return;
    const int chans = juce::jmin (2, b.getNumChannels());
    const float mid = r.getCentreY(), half = r.getHeight() * 0.48f;
    juce::Path p;
    std::vector<float> tops, bots;
    const int cols = (int) r.getWidth();
    tops.reserve ((size_t) cols); bots.reserve ((size_t) cols);
    const int step = spp > 64 ? (int) (spp / 48) : 1;   // sample a subset on long zoom-outs
    for (int x = 0; x < cols; ++x)
    {
        const auto a = (juce::int64) (startSample + x * spp);
        const auto e = (juce::int64) (startSample + (x + 1) * spp);
        float mx = 0;
        if (e > 0 && a < n)
            for (juce::int64 i = juce::jmax ((juce::int64) 0, a); i < juce::jmin ((juce::int64) n, juce::jmax (e, a + 1)); i += step)
                for (int ch = 0; ch < chans; ++ch) mx = juce::jmax (mx, std::abs (b.getSample (ch, (int) i)));
        mx = std::pow (juce::jmin (1.0f, mx * 1.25f), 0.8f);
        tops.push_back (mid - mx * half);
        bots.push_back (mid + mx * half);
    }
    if (cols < 2) return;
    p.startNewSubPath (r.getX(), tops[0]);
    for (int x = 1; x < cols; ++x) p.lineTo (r.getX() + x, tops[(size_t) x]);
    for (int x = cols - 1; x >= 0; --x) p.lineTo (r.getX() + x, bots[(size_t) x]);
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}

juce::String formatDb (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String formatPct (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
} // namespace hvx::ui
