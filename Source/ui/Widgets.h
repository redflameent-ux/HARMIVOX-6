#pragma once
#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <set>
#include <functional>

namespace hvx::ui
{
//==============================================================================
namespace Icons
{
    enum class Id { speaker, speakerOff, headphones, pencil, dots, pointer, move, scissors, marker, mic, play, stop,
                    help, plus, minus, grip, chevronDown, chevronLeft, chevronRight, sparkle, list, notes, record, check,
                    cross, folder, sync, history, undo, redo, magnet };
    void draw (juce::Graphics& g, Id id, juce::Rectangle<float> r, juce::Colour c, float stroke = 1.6f);
}

//==============================================================================
/** Shared editor state: what is selected and how the views are zoomed. */
struct UiState : public juce::ChangeBroadcaster
{
    enum Page { Import = 0, Clean, Tune, Time, Finish };
    enum Tool { Select = 0, Move, Split, Marker };

    int page = Tune;
    int selectedTrack = -1;
    std::set<int> selectedNotes;
    bool notesView = false;
    int tool = Select;

    double pxPerSec = 160.0;
    double scrollSec = -0.25;
    float noteTopMidi = 72.0f;      // pitch at the top edge of the note view
    float pxPerSemi = 15.0f;

    void notify() { sendChangeMessage(); }
};

//==============================================================================
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (const juce::String& label, double min, double max, double dflt,
          std::function<juce::String (double)> fmt = {}, double skewMid = -1.0);

    juce::Slider slider;
    std::function<void (double)> onChange;      // called while dragging
    std::function<void()> onDragStart;          // for undo checkpoints

    void setValue (double v) { slider.setValue (v, juce::dontSendNotification); repaint(); }
    double getValue() const { return slider.getValue(); }
    void setBipolar (bool b) { if (b) slider.getProperties().set ("bipolar", true); else slider.getProperties().remove ("bipolar"); }
    void setLabel (const juce::String& s) { label = s; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String label;
    std::function<juce::String (double)> format;
};

//==============================================================================
class IconButton : public juce::Button
{
public:
    IconButton (Icons::Id i, const juce::String& tip = {}, bool toggle = false);
    void setIcon (Icons::Id i) { icon = i; repaint(); }
    juce::String text;              // optional label after the icon
    bool framed = true;
    juce::Colour onColour = col::ember;

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icons::Id icon;
};

//==============================================================================
inline void styleButton (juce::Button& b, const char* kind) { b.getProperties().set ("kind", kind); }

void setupCombo (juce::ComboBox& c);

/** Small header with title, subtitle and a help "?" in the corner. */
class PanelTitle : public juce::Component
{
public:
    juce::String title, subtitle;
    std::function<void()> onHelp;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

/** Paints a lane-style waveform from a buffer into r. */
void drawWaveform (juce::Graphics& g, const juce::AudioBuffer<float>& b, juce::Rectangle<float> r,
                   double startSample, double samplesPerPixel, juce::Colour c);

juce::String formatDb (double v);
juce::String formatPct (double v);
} // namespace hvx::ui
