#pragma once
#include "../PluginProcessor.h"

class HarmivoxEditor;

/** Development helper: screenshots every page when HVX_SNAPSHOT_DIR is set. */
class DevSnapshot : private juce::Timer
{
public:
    DevSnapshot (HarmivoxEditor&, HarmivoxProcessor&, juce::File dir);
    static std::unique_ptr<DevSnapshot> createIfRequested (HarmivoxEditor&, HarmivoxProcessor&);
private:
    void timerCallback() override;
    void snap (const juce::String& name);
    void finish();
    HarmivoxEditor& editor;
    HarmivoxProcessor& proc;
    juce::File outDir;
    int step = 0, ticks = 0;
};
