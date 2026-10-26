#pragma once
#include "Context.h"

namespace hvx::ui
{
/** A step page in the left panel. */
class Page : public juce::Component
{
public:
    explicit Page (Context& c) : ctx (c) {}
    virtual void refresh() {}
    virtual juce::String title() const = 0;
    virtual juce::String subtitle() const = 0;
    virtual juce::String help() const = 0;
protected:
    Context& ctx;
};

class LeftPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit LeftPanel (Context&);
    ~LeftPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void showPage (int page);

    class Transport;
    class DragHandle;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshTrackCombo();

    Context& ctx;
    juce::ComboBox trackCombo;
    PanelTitle titleBar;
    std::vector<std::unique_ptr<Page>> pages;
    std::unique_ptr<Transport> transport;
    std::unique_ptr<DragHandle> dragHandle;
    juce::ComboBox outputCombo;
    juce::TextButton monoBtn { "Mono" }, undoBtn { "Undo" }, redoBtn { "Redo" }, historyBtn { "History" };
    std::vector<int> comboIds;
};
} // namespace hvx::ui
