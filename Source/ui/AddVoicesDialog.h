#pragma once
#include "Context.h"

namespace hvx::ui
{
/** Overlay dialog for building doubles, harmonies and stacks. */
class AddVoicesDialog : public juce::Component
{
public:
    explicit AddVoicesDialog (Context&);
    ~AddVoicesDialog() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    class VoiceRow;

private:
    void applyPreset (const HarmonyPreset& p);
    void toggleInterval (int steps);
    void rebuildRows();
    void refreshButtons();
    void create();
    juce::Rectangle<int> card() const;

    Context& ctx;
    std::vector<VoiceRequest> voices;
    IntervalMode mode = IntervalMode::FollowKey;
    juce::String presetName = "Custom", presetDesc = "Pick a preset or toggle intervals below.";

    juce::ComboBox fromCombo;
    std::vector<int> fromIds;
    juce::OwnedArray<juce::TextButton> presetBtns, ideaBtns, aboveBtns, belowBtns, modeBtns;
    juce::OwnedArray<VoiceRow> rows;
    Knob loose { "Looseness", 0.0, 1.0, 0.25, [] (double v) { return v < 0.2 ? juce::String ("tight") : v < 0.55 ? juce::String ("natural") : juce::String ("loose"); } };
    Knob others { "Other singers", 0.0, 1.0, 0.0, [] (double v) { return v < 0.01 ? juce::String ("off") : formatPct (v); } };
    Knob spread { "Pan spread", 0.0, 1.0, 1.0, formatPct };
    Knob level { "Level", -12.0, 6.0, 0.0, formatDb };
    Knob lowW { "Low width", 0.0, 2.0, 0.55, formatPct };
    Knob highW { "High width", 0.0, 2.0, 1.1, formatPct };
    Knob stz { "Stereoize", 0.0, 1.0, 0.05, formatPct };
    juce::ToggleButton imager { "Imager" }, stzOn { "Stereoize" }, useProfile { "Shape with my voice profile" };
    juce::TextButton cancelBtn { "Cancel" }, createBtn { "Create voices" };
    juce::Rectangle<int> listArea;
};
} // namespace hvx::ui
