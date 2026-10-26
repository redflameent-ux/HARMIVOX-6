#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hvx::ui
{
namespace col
{
    const juce::Colour bg        { 0xff0c0d11 };
    const juce::Colour panel     { 0xff13141a };
    const juce::Colour panel2    { 0xff181920 };
    const juce::Colour raised    { 0xff1f2029 };
    const juce::Colour raisedHi  { 0xff282a35 };
    const juce::Colour border    { 0xff262833 };
    const juce::Colour borderHi  { 0xff383b49 };
    const juce::Colour grid      { 0xff1b1c24 };
    const juce::Colour text      { 0xffece8e3 };
    const juce::Colour textDim   { 0xff9a9cab };
    const juce::Colour textFaint { 0xff5f6272 };
    const juce::Colour ember     { 0xffff4433 };
    const juce::Colour flame     { 0xffc8102e };
    const juce::Colour amber     { 0xffffb547 };
    const juce::Colour good      { 0xff4fd18b };
    const juce::Colour bad       { 0xffff4d5e };
}

struct Fonts
{
    static juce::Font body (float h = 14.0f);
    static juce::Font medium (float h = 14.0f);
    static juce::Font semi (float h = 14.0f);
    static juce::Font bold (float h = 14.0f);
    static juce::Font display (float h = 22.0f);       // condensed bold
    static juce::Font displaySemi (float h = 16.0f);   // condensed semibold
    static juce::Font mono (float h = 14.0f);
    static juce::Typeface::Ptr regularFace();
};

juce::ColourGradient accentGradient (juce::Rectangle<float> r, float alpha = 1.0f);

/** Small caps-style section label ("KEY & SCALE"). */
void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c = col::textFaint);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String&, bool isSeparator, int standardHeight, int& w, int& h) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;

    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
};
} // namespace hvx::ui
