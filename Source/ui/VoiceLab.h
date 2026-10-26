#pragma once
#include "Context.h"

namespace hvx::ui
{
/** Pick which voice profile shapes new voices, or start a guided session. */
class VoiceLabPicker : public juce::Component
{
public:
    explicit VoiceLabPicker (Context&);
    std::function<void()> onClose, onStartSession;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void reload();

private:
    juce::Rectangle<int> card() const;
    juce::Rectangle<int> rowRect (int i) const;
    Context& ctx;
    juce::Array<juce::File> files;
    juce::TextButton startBtn { "Start guided session" }, closeBtn { "Close" }, deleteBtn { "Delete" };
    juce::Rectangle<int> listArea;
};

/** The guided ~10 minute recording session. */
class VoiceLabSession : public juce::Component, private juce::Timer
{
public:
    explicit VoiceLabSession (Context&);
    ~VoiceLabSession() override;
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    struct Exercise
    {
        juce::String group, prompt, detail;
        std::vector<std::pair<int, double>> guide;  // semitone offset from base, seconds
        double seconds = 6.0;
        bool pitched = true;
        double clickBpm = 0.0;
        juce::String kind;
    };

private:
    enum class State { Welcome, Ready, CountIn, Recording, Result, Building, Done };
    void timerCallback() override;
    void startTake();
    void finishTake();
    void next();
    void build();
    void layoutButtons();
    int baseMidi() const;
    std::vector<juce::String> groups() const;

    Context& ctx;
    std::vector<Exercise> ex;
    std::map<int, std::vector<std::shared_ptr<const SourceAudio>>> takes;   // exercise -> takes
    State state = State::Welcome;
    int current = 0;
    double phaseStart = 0.0;
    juce::String resultText;
    bool resultOk = false;
    float livePitch = 0.0f;
    int round = 1;

    juce::TextEditor nameEdit;
    juce::ComboBox rangeCombo;
    juce::TextButton primaryBtn { "Start" }, secondaryBtn { "Redo" }, skipBtn { "Skip" }, closeBtn { "Close" };
    juce::Slider guideVol;
    std::atomic<bool> building { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::String builtName;
};
} // namespace hvx::ui
