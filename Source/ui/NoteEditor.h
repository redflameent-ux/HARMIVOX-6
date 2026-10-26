#pragma once
#include "Context.h"

namespace hvx::ui
{
class NoteEditor : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit NoteEditor (Context&);
    ~NoteEditor() override;

    static constexpr int keysW = 54;
    static constexpr int rulerH = 28;
    static constexpr int barH = 112;

    void resized() override;
    void paint (juce::Graphics&) override;
    void centreOnTrack();

    class Grid;
    class NoteBar;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    Context& ctx;
    std::unique_ptr<Grid> grid;
    std::unique_ptr<NoteBar> bar;
    int lastTrack = -1;
};
} // namespace hvx::ui
