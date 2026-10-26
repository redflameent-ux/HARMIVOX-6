#include "MainEditor.h"
#include "SidePanels.h"
#include "TrackLanes.h"
#include "NoteEditor.h"
#include "AddVoicesDialog.h"
#include "VoiceLab.h"
#include "DevSnapshot.h"
#include "BinaryData.h"

using namespace hvx;
using namespace hvx::ui;

namespace
{
constexpr int kHeaderH = 76;
constexpr int kLeftW = 316;
constexpr int kToolbarH = 52;

class HelpBody : public juce::Component
{
public:
    HelpBody (juce::String t, juce::String b) : title (std::move (t)), body (std::move (b))
    {
        juce::AttributedString s;
        s.append (body, Fonts::body (13.5f), col::text);
        layout.createLayout (s, 380.0f);
        setSize (412, (int) layout.getHeight() + 62);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::text);
        g.setFont (Fonts::display (20.0f));
        g.drawText (title, 16, 10, getWidth() - 32, 26, juce::Justification::centredLeft);
        layout.draw (g, juce::Rectangle<float> (16, 42, 380, layout.getHeight()));
    }
    juce::String title, body;
    juce::TextLayout layout;
};

const char* hints[] = {
    "Arm capture, then press play in FL. Or drop audio files here.",
    "Clean settings apply to the selected take and every voice built from it.",
    "Double-click a lane to edit its notes",
    "Double-click a clip to set a timing marker. Drag markers to move them.",
    "One chain for the whole stack, or per track.",
};
} // namespace

//==============================================================================
HarmivoxEditor::HarmivoxEditor (HarmivoxProcessor& p)
    : AudioProcessorEditor (p), proc (p), ctx { p, p.session, ui }
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    ctx.openAddVoices = [this] { openAddVoices(); };
    ctx.openVoiceLab  = [this] { openVoiceLab(); };
    ctx.showHelp = [this] (const juce::String& t, const juce::String& b, juce::Component* c) { showHelp (t, b, c); };
    ctx.toast = [this] (const juce::String& s) { toast (s); };

    ui.page = juce::jlimit (0, 4, proc.lastPage == 0 && ! proc.session.doc.tracks.empty() ? 2 : proc.lastPage);
    if (proc.session.doc.tracks.empty()) ui.page = UiState::Import;

    const char* names[] = { "Import", "Clean", "Tune", "Time", "Finish" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        styleButton (*b, "tab");
        b->onClick = [this, i] { setPage (i); };
        addAndMakeVisible (b);
    }

    helpBtn = std::make_unique<IconButton> (Icons::Id::help, "How this page works");
    helpBtn->onClick = [this]
    {
        static const char* bodies[] = {
            "1  Import: capture a take from your FL insert or load files.\n2  Clean: remove noise, breaths and clicks.\n"
            "3  Tune: correct pitch and add voices.\n4  Time: tighten timing.\n5  Finish: vocal chain and stereo.\n\n"
            "Space plays/stops here. Ctrl+Z undoes. Tab switches All tracks / Notes. Keys 1-5 jump between steps." };
        showHelp ("HARMIVOX-6", bodies[0], helpBtn.get());
    };
    voiceLabBtn = std::make_unique<IconButton> (Icons::Id::mic, "Voice lab: teach HARMIVOX-6 your voice");
    voiceLabBtn->text = "Voice lab";
    voiceLabBtn->onClick = [this] { openVoiceLab(); };

    allTracksBtn = std::make_unique<IconButton> (Icons::Id::list, "All tracks");
    allTracksBtn->text = "All tracks";
    notesBtn = std::make_unique<IconButton> (Icons::Id::notes, "Notes");
    notesBtn->text = "Notes";
    allTracksBtn->onClick = [this] { setNotesView (false); };
    notesBtn->onClick = [this] { setNotesView (true); };

    prevTrack = std::make_unique<IconButton> (Icons::Id::chevronLeft, "Previous track");
    nextTrack = std::make_unique<IconButton> (Icons::Id::chevronRight, "Next track");
    auto step = [this] (int d)
    {
        auto& tr = proc.session.doc.tracks;
        if (tr.empty()) return;
        int i = juce::jmax (0, proc.session.doc.indexOf (ui.selectedTrack));
        i = (i + d + (int) tr.size()) % (int) tr.size();
        ctx.select (tr[(size_t) i].id);
    };
    prevTrack->onClick = [step] { step (-1); };
    nextTrack->onClick = [step] { step (1); };
    soloBtn = std::make_unique<IconButton> (Icons::Id::headphones, "Solo the track you're editing");
    soloBtn->text = "Solo";
    soloBtn->setClickingTogglesState (true);
    soloBtn->onColour = col::amber;
    soloBtn->onClick = [this]
    {
        for (auto& t : proc.session.doc.tracks) t.solo = soloBtn->getToggleState() && t.id == ui.selectedTrack;
        proc.session.changed();
    };

    struct ToolDef { Icons::Id icon; const char* tip; };
    const ToolDef defs[] = { { Icons::Id::pointer, "Select & edit (pitch, edges, transitions)" },
                             { Icons::Id::move, "Move: drag clips or notes in time (Shift = no snap)" },
                             { Icons::Id::scissors, "Split: click a note to cut it" },
                             { Icons::Id::marker, "Warp marker: click a clip to add a timing marker" } };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = tools.add (new IconButton (defs[i].icon, defs[i].tip));
        b->onClick = [this, i] { ui.tool = i; ui.notify(); };
        addAndMakeVisible (b);
    }
    for (auto* b : { helpBtn.get(), voiceLabBtn.get(), allTracksBtn.get(), notesBtn.get(), prevTrack.get(), nextTrack.get(), soloBtn.get() })
        addAndMakeVisible (b);

    left = std::make_unique<LeftPanel> (ctx);
    lanes = std::make_unique<TrackLanes> (ctx);
    notes = std::make_unique<NoteEditor> (ctx);
    lanes->onOpenNotes = [this] (int id) { ctx.select (id); setNotesView (true); };
    addAndMakeVisible (*left);
    addAndMakeVisible (*lanes);
    addChildComponent (*notes);

    proc.session.addChangeListener (this);
    ui.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (1100, 720, 2400, 1500);
    setSize (juce::jlimit (1100, 2400, proc.editorW), juce::jlimit (720, 1500, proc.editorH));
    setPage (ui.page);
    startTimerHz (20);
    devSnapshot = DevSnapshot::createIfRequested (*this, proc);
}

HarmivoxEditor::~HarmivoxEditor()
{
    devSnapshot.reset();
    proc.session.removeChangeListener (this);
    ui.removeChangeListener (this);
    closeOverlays();
    notes.reset();
    lanes.reset();
    left.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void HarmivoxEditor::setPage (int page)
{
    ui.page = juce::jlimit (0, 4, page);
    proc.lastPage = ui.page;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == ui.page, juce::dontSendNotification);
    left->showPage (ui.page);
    if (ui.page == UiState::Time && ui.tool == UiState::Split) ui.tool = UiState::Select;
    ui.notify();
    resized();
}

void HarmivoxEditor::setNotesView (bool n)
{
    ui.notesView = n;
    ui.notify();
}

void HarmivoxEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const bool n = ui.notesView;
    if (notes->isVisible() != n || lanes->isVisible() == n)
    {
        notes->setVisible (n);
        lanes->setVisible (! n);
        if (n) notes->centreOnTrack();
        resized();
    }
    allTracksBtn->setToggleState (! n, juce::dontSendNotification);
    notesBtn->setToggleState (n, juce::dontSendNotification);
    for (int i = 0; i < tools.size(); ++i) tools[i]->setToggleState (ui.tool == i, juce::dontSendNotification);
    bool anySolo = false;
    for (auto& t : proc.session.doc.tracks) anySolo |= t.solo;
    soloBtn->setToggleState (anySolo, juce::dontSendNotification);
    repaint (toolbarArea);
}

void HarmivoxEditor::timerCallback()
{
    if (toastText.isNotEmpty() && juce::Time::getMillisecondCounter() - toastTime > 2800)
    {
        toastText.clear();
        repaint();
    }
    // keep the editor size with the project
    proc.editorW = getWidth();
    proc.editorH = getHeight();
}

//==============================================================================
void HarmivoxEditor::layoutToolbar (juce::Rectangle<int> r)
{
    toolbarArea = r;
    auto row = r.reduced (12, 9);
    allTracksBtn->setBounds (row.removeFromLeft (112));
    row.removeFromLeft (4);
    notesBtn->setBounds (row.removeFromLeft (92));
    row.removeFromLeft (18);

    const bool n = ui.notesView;
    prevTrack->setVisible (n);
    nextTrack->setVisible (n);
    soloBtn->setVisible (n);
    trackNameArea = {};
    if (n)
    {
        prevTrack->setBounds (row.removeFromLeft (34));
        row.removeFromLeft (4);
        nextTrack->setBounds (row.removeFromLeft (34));
        row.removeFromLeft (8);
        trackNameArea = row.removeFromLeft (170);
        row.removeFromLeft (8);
    }
    for (int i = 0; i < tools.size(); ++i)
    {
        const bool show = ! (n && i == UiState::Marker);
        tools[i]->setVisible (show);
        if (! show) continue;
        tools[i]->setBounds (row.removeFromLeft (34));
        row.removeFromLeft (6);
    }
    voiceLabBtn->setBounds (row.removeFromRight (124));
    row.removeFromRight (8);
    if (n) { soloBtn->setBounds (row.removeFromRight (84)); row.removeFromRight (8); }
    hintArea = row.withTrimmedLeft (12);
}

void HarmivoxEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (kHeaderH);
    {
        auto h = headerArea.reduced (16, 12);
        helpBtn->setBounds (h.removeFromRight (40));
        auto tabsArea = headerArea.withSizeKeepingCentre (juce::jmin (640, getWidth() - 660), 40).translated (40, 0);
        const int w = tabsArea.getWidth() / 5;
        for (auto* t : tabs) t->setBounds (tabsArea.removeFromLeft (w).reduced (4, 0));
    }
    left->setBounds (r.removeFromLeft (kLeftW));
    layoutToolbar (r.removeFromTop (kToolbarH));
    lanes->setBounds (r);
    notes->setBounds (r);

    for (juce::Component* o : { (juce::Component*) addVoices.get(), (juce::Component*) labPicker.get(), (juce::Component*) labSession.get() })
        if (o != nullptr) o->setBounds (getLocalBounds());
}

void HarmivoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // header
    g.setColour (col::panel);
    g.fillRect (headerArea);
    g.setColour (col::border);
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0, (float) getWidth());

    // logo artwork
    static const juce::Image logoImg = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    if (logoImg.isValid())
    {
        auto area = headerArea.reduced (14, 5).withWidth (300).toFloat();
        g.drawImage (logoImg, area, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid | juce::RectanglePlacement::onlyReduceInSize);
    }

    // tab numbers
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto b = tabs[i]->getBounds().toFloat();
        auto c = juce::Rectangle<float> (b.getX() + 12, b.getCentreY() - 10, 20, 20);
        juce::ignoreUnused (c);
    }

    // toolbar
    g.setColour (col::panel2);
    g.fillRect (toolbarArea);
    g.setColour (col::border);
    g.drawHorizontalLine (toolbarArea.getBottom() - 1, (float) toolbarArea.getX(), (float) toolbarArea.getRight());
    if (! trackNameArea.isEmpty())
        if (auto* t = proc.session.doc.find (ui.selectedTrack))
        {
            g.setColour (t->colour);
            g.fillEllipse ((float) trackNameArea.getX(), trackNameArea.getCentreY() - 4.0f, 8, 8);
            g.setColour (col::text);
            g.setFont (Fonts::semi (14.0f));
            g.drawText (t->name, trackNameArea.withTrimmedLeft (16), juce::Justification::centredLeft, true);
        }
    g.setColour (col::textFaint);
    g.setFont (Fonts::body (12.5f));
    juce::String hint = ui.notesView ? "Drag notes up and down. Hover a join between notes to shape the transition." : hints[juce::jlimit (0, 4, ui.page)];
    g.drawText (hint, hintArea, juce::Justification::centredLeft, true);
}

void HarmivoxEditor::paintOverChildren (juce::Graphics& g)
{
    // step numbers drawn over the tab buttons (not while a dialog covers them)
    const bool overlay = addVoices != nullptr || labPicker != nullptr || labSession != nullptr;
    for (int i = 0; i < (overlay ? 0 : tabs.size()); ++i)
    {
        auto b = tabs[i]->getBounds().toFloat();
        const bool on = i == ui.page;
        const float tw = juce::GlyphArrangement::getStringWidth (Fonts::semi (13.5f), tabs[i]->getButtonText());
        auto c = juce::Rectangle<float> (b.getCentreX() - tw * 0.5f - 30.0f, b.getCentreY() - 10.0f, 20.0f, 20.0f);
        if (on) { g.setGradientFill (accentGradient (c)); g.fillEllipse (c); g.setColour (juce::Colour (0xff1a0b0b)); }
        else { g.setColour (col::borderHi); g.drawEllipse (c.reduced (0.5f), 1.0f); g.setColour (col::textDim); }
        g.setFont (Fonts::bold (11.5f));
        g.drawText (juce::String (i + 1), c, juce::Justification::centred);
    }

    if (dragOver)
    {
        auto r = getLocalBounds().toFloat().reduced (6);
        g.setColour (col::ember.withAlpha (0.08f));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (col::ember);
        g.drawRoundedRectangle (r, 12.0f, 2.0f);
        g.setColour (col::text);
        g.setFont (Fonts::display (26.0f));
        g.drawText ("Drop to import as a new take (starts at bar 1)", r, juce::Justification::centred);
    }
    if (toastText.isNotEmpty())
    {
        const float tw = juce::GlyphArrangement::getStringWidth (Fonts::semi (14.0f), toastText) + 40.0f;
        auto t = juce::Rectangle<float> (tw, 40.0f).withCentre ({ getWidth() * 0.5f + kLeftW * 0.5f, (float) getHeight() - 48.0f });
        g.setColour (col::raisedHi);
        g.fillRoundedRectangle (t, 10.0f);
        g.setGradientFill (accentGradient (t, 0.9f));
        g.drawRoundedRectangle (t, 10.0f, 1.2f);
        g.setColour (col::text);
        g.setFont (Fonts::semi (14.0f));
        g.drawText (toastText, t, juce::Justification::centred);
    }
}

//==============================================================================
void HarmivoxEditor::openAddVoices()
{
    if (proc.session.doc.lead() == nullptr) { toast ("Import a take first."); return; }
    closeOverlays();
    addVoices = std::make_unique<AddVoicesDialog> (ctx);
    addVoices->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<HarmivoxEditor> (this)] { if (sp) { sp->addVoices.reset(); sp->repaint(); } }); };
    addAndMakeVisible (*addVoices);
    addVoices->setBounds (getLocalBounds());
    addVoices->grabKeyboardFocus();
}

void HarmivoxEditor::openVoiceLab()
{
    closeOverlays();
    labPicker = std::make_unique<VoiceLabPicker> (ctx);
    labPicker->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<HarmivoxEditor> (this)] { if (sp) sp->labPicker.reset(); }); };
    labPicker->onStartSession = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<HarmivoxEditor> (this)] { if (sp) sp->openVoiceSession(); }); };
    addAndMakeVisible (*labPicker);
    labPicker->setBounds (getLocalBounds());
    labPicker->grabKeyboardFocus();
}

void HarmivoxEditor::openVoiceSession()
{
    closeOverlays();
    labSession = std::make_unique<VoiceLabSession> (ctx);
    labSession->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<HarmivoxEditor> (this)] { if (sp) { sp->labSession.reset(); sp->openVoiceLab(); } }); };
    addAndMakeVisible (*labSession);
    labSession->setBounds (getLocalBounds());
    labSession->grabKeyboardFocus();
}

void HarmivoxEditor::closeOverlays()
{
    addVoices.reset();
    labPicker.reset();
    labSession.reset();
}

void HarmivoxEditor::showHelp (const juce::String& title, const juce::String& body, juce::Component* near)
{
    auto area = near != nullptr ? near->getScreenBounds() : getScreenBounds().withSizeKeepingCentre (10, 10);
    juce::CallOutBox::launchAsynchronously (std::make_unique<HelpBody> (title, body), area, nullptr);
}

void HarmivoxEditor::toast (const juce::String& s)
{
    toastText = s;
    toastTime = juce::Time::getMillisecondCounter();
    repaint();
}

//==============================================================================
bool HarmivoxEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k == juce::KeyPress::spaceKey) { proc.setInternalPlaying (! proc.isInternalPlaying()); return true; }
    if (k.getKeyCode() == 'Z' && mods.isCommandDown() && mods.isShiftDown()) { proc.session.redo(); return true; }
    if (k.getKeyCode() == 'Z' && mods.isCommandDown()) { proc.session.undo(); return true; }
    if (k.getKeyCode() == 'Y' && mods.isCommandDown()) { proc.session.redo(); return true; }
    if (k == juce::KeyPress::tabKey) { setNotesView (! ui.notesView); return true; }
    if (! mods.isAnyModifierKeyDown() && k.getKeyCode() >= '1' && k.getKeyCode() <= '5') { setPage (k.getKeyCode() - '1'); return true; }
    if (k.getKeyCode() == juce::KeyPress::homeKey) { proc.setInternalPosition (0.0); return true; }
    return false;
}

void HarmivoxEditor::mouseDown (const juce::MouseEvent&) { grabKeyboardFocus(); }

bool HarmivoxEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void HarmivoxEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    int n = 0;
    for (auto& f : files)
        if (proc.session.importFile (juce::File (f), 0.0)) ++n;
    if (n > 0) toast ("Importing " + juce::String (n) + (n == 1 ? " take..." : " takes..."));
    repaint();
}
