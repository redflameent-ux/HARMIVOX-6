#pragma once
#include "Context.h"

namespace hvx::ui
{
class TrackLanes : public juce::Component, private juce::ChangeListener, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit TrackLanes (Context&);
    ~TrackLanes() override;

    static constexpr int headerW = 236;
    static constexpr int laneH = 88;
    static constexpr int rulerH = 28;
    static constexpr int chordH = 34;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void refresh();
    std::function<void (int trackId)> onOpenNotes;
    int animTickValue() const;

    class LaneHeader;
    class Content;
    class Ruler;
    class ChordLane;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    Context& ctx;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<ChordLane> chords;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;
    juce::ScrollBar hbar { false };
    double lastPos = -1;
    int animTick = 0;
};
} // namespace hvx::ui
