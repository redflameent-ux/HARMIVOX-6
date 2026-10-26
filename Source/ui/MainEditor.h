#pragma once
#include "../PluginProcessor.h"
#include "Context.h"

namespace hvx::ui
{
class LeftPanel;
class TrackLanes;
class NoteEditor;
class AddVoicesDialog;
class VoiceLabPicker;
class VoiceLabSession;
}

class DevSnapshot;

class HarmivoxEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit HarmivoxEditor (HarmivoxProcessor&);
    ~HarmivoxEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    void setPage (int page);
    void setNotesView (bool notes);
    void openAddVoices();
    void openVoiceLab();
    void openVoiceSession();
    void closeOverlays();
    hvx::ui::UiState& uiState() { return ui; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showHelp (const juce::String& title, const juce::String& body, juce::Component* near);
    void toast (const juce::String&);
    void layoutToolbar (juce::Rectangle<int>);

    HarmivoxProcessor& proc;
    hvx::ui::LookAndFeel lnf;
    hvx::ui::UiState ui;
    hvx::ui::Context ctx;
    juce::TooltipWindow tooltips { this, 600 };

    juce::OwnedArray<juce::TextButton> tabs;
    std::unique_ptr<hvx::ui::IconButton> helpBtn, voiceLabBtn, allTracksBtn, notesBtn, prevTrack, nextTrack, soloBtn;
    juce::OwnedArray<hvx::ui::IconButton> tools;

    std::unique_ptr<hvx::ui::LeftPanel> left;
    std::unique_ptr<hvx::ui::TrackLanes> lanes;
    std::unique_ptr<hvx::ui::NoteEditor> notes;
    std::unique_ptr<hvx::ui::AddVoicesDialog> addVoices;
    std::unique_ptr<hvx::ui::VoiceLabPicker> labPicker;
    std::unique_ptr<hvx::ui::VoiceLabSession> labSession;

    juce::Rectangle<int> headerArea, toolbarArea, hintArea, trackNameArea;
    juce::String toastText;
    juce::uint32 toastTime = 0;
    bool dragOver = false;
    std::unique_ptr<DevSnapshot> devSnapshot;
};
