#include "SidePanels.h"
#include "../engine/Finish.h"

namespace hvx::ui
{
namespace
{
const juce::String dot = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"));

/** Lays knobs out in rows of up to `perRow`. */
juce::Rectangle<int> layoutKnobs (juce::Rectangle<int>& r, std::initializer_list<juce::Component*> knobs, int perRow, int h = 86)
{
    std::vector<juce::Component*> v (knobs);
    auto used = r.withHeight (0);
    for (size_t i = 0; i < v.size(); i += (size_t) perRow)
    {
        auto row = r.removeFromTop (h);
        used = used.getUnion (row);
        const int w = row.getWidth() / perRow;
        for (size_t k = i; k < juce::jmin (v.size(), i + (size_t) perRow); ++k) v[k]->setBounds (row.removeFromLeft (w));
    }
    return used;
}

void divider (juce::Graphics& g, int y, int w)
{
    g.setColour (col::border);
    g.drawHorizontalLine (y, 16.0f, (float) w - 16.0f);
}

/** The take that owns tuning for the selected track (voices follow their take). */
Track* tuneOwner (Context& ctx)
{
    auto* t = ctx.selected();
    if (t != nullptr && t->isVoice()) return ctx.session.doc.find (t->voice.parentId);
    return t;
}
} // namespace

//==============================================================================
class ImportPage : public Page, private juce::Timer
{
public:
    explicit ImportPage (Context& c) : Page (c)
    {
        styleButton (armBtn, "primary");
        styleButton (fileBtn, "secondary");
        styleButton (cancelBtn, "secondary");
        styleButton (leadBtn, "secondary");
        for (auto* b : { &armBtn, &fileBtn, &cancelBtn, &leadBtn }) addAndMakeVisible (b);
        armBtn.onClick = [this]
        {
            const auto st = ctx.proc.captureState();
            if (st == HarmivoxProcessor::Capture::Off) ctx.proc.armHostCapture();
            else ctx.proc.stopHostCapture();
            refresh();
        };
        cancelBtn.onClick = [this] { ctx.proc.cancelCapture(); refresh(); };
        fileBtn.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a vocal take", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [this] (const juce::FileChooser& fc)
            {
                for (auto& f : fc.getResults()) ctx.session.importFile (f, 0.0);
            });
        };
        leadBtn.onClick = [this] { if (auto* t = ctx.selected()) ctx.session.setLead (t->id); };
        startTimerHz (8);
    }

    juce::String title() const override { return "Import"; }
    juce::String subtitle() const override { return "Bring your vocal takes in."; }
    juce::String help() const override
    {
        return "Put HARMIVOX-6 on the mixer insert your vocal plays through. Click Arm capture, then play the section in FL: "
               "the take is recorded and lined up with your song automatically. Stop FL when the section is done.\n\n"
               "You can also load or drop WAV, AIFF, FLAC or MP3 files: they start at bar 1.\n\n"
               "The first take becomes the LEAD. Every harmony you add is built from a take.";
    }

    void timerCallback() override { if (isVisible()) refresh(); }

    void refresh() override
    {
        const auto st = ctx.proc.captureState();
        armBtn.setButtonText (st == HarmivoxProcessor::Capture::Off ? "Arm capture"
                              : st == HarmivoxProcessor::Capture::WaitingForHost ? "Waiting... (click to cancel)" : "Stop & keep take");
        cancelBtn.setVisible (st == HarmivoxProcessor::Capture::RecordingHost);
        auto* t = ctx.selected();
        leadBtn.setEnabled (t != nullptr && ! t->isVoice() && ! t->isLead);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        r.removeFromTop (26);
        armBtn.setBounds (r.removeFromTop (40));
        r.removeFromTop (8);
        auto row = r.removeFromTop (30);
        cancelBtn.setBounds (row.removeFromRight (90));
        statusArea = row;
        r.removeFromTop (34);
        fileBtn.setBounds (r.removeFromTop (36));
        r.removeFromTop (54);
        takesArea = r.removeFromTop (juce::jmin (r.getHeight() - 50, 150));
        r.removeFromTop (8);
        leadBtn.setBounds (r.removeFromTop (32));
    }

    void paint (juce::Graphics& g) override
    {
        drawSectionLabel (g, "Capture from FL", { 16, 4, 200, 18 });
        const auto st = ctx.proc.captureState();
        g.setFont (Fonts::medium (13.0f));
        if (st == HarmivoxProcessor::Capture::WaitingForHost)
        {
            g.setColour (col::amber);
            g.drawText ("Press play or record in FL", statusArea, juce::Justification::centredLeft);
        }
        else if (st == HarmivoxProcessor::Capture::RecordingHost)
        {
            const bool blink = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
            g.setColour (col::bad.withAlpha (blink ? 1.0f : 0.4f));
            g.fillEllipse ((float) statusArea.getX(), statusArea.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setColour (col::text);
            const double s = ctx.proc.captureSeconds();
            g.drawText ("Recording  " + juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2),
                        statusArea.withTrimmedLeft (18), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (col::textFaint);
            const float lvl = ctx.proc.inputLevel();
            auto meter = statusArea.toFloat().withSizeKeepingCentre ((float) statusArea.getWidth(), 6.0f);
            g.setColour (col::raised);
            g.fillRoundedRectangle (meter, 3.0f);
            g.setGradientFill (accentGradient (meter));
            g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, std::pow (lvl, 0.5f))), 3.0f);
        }

        drawSectionLabel (g, "Or load audio", { 16, fileBtn.getY() - 24, 200, 18 });
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.5f));
        g.drawText ("or drop files anywhere on HARMIVOX-6", 16, fileBtn.getBottom() + 4, getWidth() - 32, 18, juce::Justification::centredLeft);

        drawSectionLabel (g, "Takes", { 16, takesArea.getY() - 22, 200, 18 });
        auto r = takesArea;
        int shown = 0;
        for (auto& t : ctx.session.doc.tracks)
        {
            if (t.isVoice()) continue;
            auto row = r.removeFromTop (30);
            if (row.getHeight() < 20) break;
            const bool sel = ctx.ui.selectedTrack == t.id;
            g.setColour (sel ? col::raisedHi : col::raised);
            g.fillRoundedRectangle (row.toFloat().reduced (0, 2), 6.0f);
            g.setColour (t.colour);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) row.getX() + 6, (float) row.getY() + 8, 3, 14), 1.5f);
            g.setColour (col::text);
            g.setFont (Fonts::semi (13.0f));
            g.drawText (t.name, row.withTrimmedLeft (16), juce::Justification::centredLeft);
            g.setColour (t.isLead ? col::ember : col::textFaint);
            g.setFont (Fonts::medium (12.0f));
            g.drawText ((t.isLead ? "LEAD  " + dot + "  " : juce::String()) + (t.source ? juce::String (t.source->lengthSeconds(), 1) + "s" : juce::String()),
                        row.withTrimmedRight (10), juce::Justification::centredRight);
            ++shown;
        }
        if (ctx.session.pendingImports() > 0)
        {
            g.setColour (col::amber);
            g.setFont (Fonts::medium (13.0f));
            g.drawText ("Listening to the take...", r.removeFromTop (28), juce::Justification::centredLeft);
        }
        else if (shown == 0)
        {
            g.setColour (col::textFaint);
            g.setFont (Fonts::body (13.0f));
            g.drawFittedText ("No takes yet.", r.removeFromTop (28), juce::Justification::centredLeft, 1);
        }

        auto& d = ctx.session.doc;
        if (d.lead() != nullptr)
        {
            g.setColour (col::textDim);
            g.setFont (Fonts::medium (12.5f));
            g.drawText ("Key: " + keyNames()[d.key] + " " + scaleNames()[d.scale] + (d.autoKey ? "  (detected)" : ""),
                        16, leadBtn.getBottom() + 10, getWidth() - 32, 18, juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! takesArea.contains (e.getPosition())) return;
        int i = (e.y - takesArea.getY()) / 30, k = 0;
        for (auto& t : ctx.session.doc.tracks)
        {
            if (t.isVoice()) continue;
            if (k++ == i) { ctx.select (t.id); return; }
        }
    }

    juce::TextButton armBtn { "Arm capture" }, fileBtn { "Load audio file..." }, cancelBtn { "Discard" }, leadBtn { "Set selected take as lead" };
    juce::Rectangle<int> statusArea, takesArea;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class CleanPage : public Page
{
public:
    explicit CleanPage (Context& c) : Page (c)
    {
        auto bind = [this] (Knob& k, std::function<void (CleanSettings&, double)> set)
        {
            addAndMakeVisible (k);
            k.onDragStart = [this] { ctx.session.checkpoint ("Clean"); };
            k.onChange = [this, set] (double v)
            {
                if (updating) return;
                if (auto* t = tuneOwner (ctx)) { set (t->clean, v); ctx.session.changed(); }
            };
        };
        bind (noise, [] (CleanSettings& s, double v) { s.noiseReduce = (float) v; });
        bind (breath, [] (CleanSettings& s, double v) { s.breathDb = (float) v; });
        bind (gate, [] (CleanSettings& s, double v) { s.gate = (float) v; });
        bind (click, [] (CleanSettings& s, double v) { s.deClick = (float) v; });
        styleButton (allBtn, "secondary");
        styleButton (resetBtn, "ghost");
        addAndMakeVisible (allBtn);
        addAndMakeVisible (resetBtn);
        allBtn.onClick = [this]
        {
            auto* src = tuneOwner (ctx);
            if (src == nullptr) return;
            ctx.session.checkpoint ("Clean all takes");
            const auto s = src->clean;
            for (auto& t : ctx.session.doc.tracks) if (! t.isVoice()) t.clean = s;
            ctx.session.changed();
        };
        resetBtn.onClick = [this]
        {
            if (auto* t = tuneOwner (ctx)) { ctx.session.checkpoint ("Reset clean"); t->clean = {}; ctx.session.changed(); }
        };
    }
    juce::String title() const override { return "Clean"; }
    juce::String subtitle() const override { return "Tidy the take before tuning."; }
    juce::String help() const override
    {
        return "Noise reduce learns the hiss and room tone from the quiet parts and pulls it down.\n\n"
               "Breaths turns down long breaths between phrases (it does not delete them).\n\n"
               "Gate quiets everything between phrases. De-click repairs mouth clicks and pops.\n\n"
               "Voices built from a take use that take's Clean settings.";
    }
    void refresh() override
    {
        updating = true;
        auto* t = tuneOwner (ctx);
        for (auto* k : { &noise, &breath, &gate, &click }) k->setEnabled (t != nullptr);
        if (t) { noise.setValue (t->clean.noiseReduce); breath.setValue (t->clean.breathDb); gate.setValue (t->clean.gate); click.setValue (t->clean.deClick); }
        updating = false;
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        r.removeFromTop (26);
        layoutKnobs (r, { &noise, &breath, &gate, &click }, 2, 92);
        r.removeFromTop (14);
        allBtn.setBounds (r.removeFromTop (36));
        r.removeFromTop (6);
        resetBtn.setBounds (r.removeFromTop (30));
    }
    void paint (juce::Graphics& g) override
    {
        auto* sel = ctx.selected();
        drawSectionLabel (g, "Cleanup", { 16, 4, 200, 18 });
        if (sel && sel->isVoice())
        {
            g.setColour (col::textFaint);
            g.setFont (Fonts::body (12.5f));
            g.drawFittedText ("Showing the take this voice is built from.", 16, resetBtn.getBottom() + 12, getWidth() - 32, 32, juce::Justification::topLeft, 2);
        }
    }
    bool updating = false;
    Knob noise { "Noise reduce", 0.0, 1.0, 0.0, formatPct };
    Knob breath { "Breaths", 0.0, 24.0, 0.0, [] (double v) { return v < 0.1 ? juce::String ("off") : "-" + juce::String (v, 1) + " dB"; } };
    Knob gate { "Gate", 0.0, 1.0, 0.0, formatPct };
    Knob click { "De-click", 0.0, 1.0, 0.0, formatPct };
    juce::TextButton allBtn { "Use on every take" }, resetBtn { "Reset" };
};

//==============================================================================
class TunePage : public Page
{
public:
    explicit TunePage (Context& c) : Page (c)
    {
        keyCombo.addItem ("Auto", 1);
        for (int k = 0; k < 12; ++k) keyCombo.addItem (keyNames()[k], 2 + k);
        for (int s = 0; s < scaleNames().size(); ++s) scaleCombo.addItem (scaleNames()[s], 1 + s);
        presetCombo.addItemList ({ "Natural", "Pop", "Hard tune", "Custom" }, 1);
        for (auto* cb : { &keyCombo, &scaleCombo, &presetCombo }) { setupCombo (*cb); addAndMakeVisible (cb); }

        keyCombo.onChange = [this]
        {
            if (updating) return;
            ctx.session.checkpoint ("Key");
            const int id = keyCombo.getSelectedId();
            ctx.session.setKey (id == 1, id == 1 ? ctx.session.doc.key : id - 2, ctx.session.doc.scale);
        };
        scaleCombo.onChange = [this]
        {
            if (updating) return;
            ctx.session.checkpoint ("Scale");
            ctx.session.setKey (false, ctx.session.doc.key, scaleCombo.getSelectedId() - 1);
        };
        presetCombo.onChange = [this]
        {
            if (updating) return;
            auto* t = tuneOwner (ctx);
            if (t == nullptr) return;
            ctx.session.checkpoint ("Tuning preset");
            const int p = presetCombo.getSelectedId() - 1;
            t->tune.preset = p;
            if (p == 0) { t->tune.strength = 0.55f; t->tune.retuneMs = 120; t->tune.driftFix = 0.0f; t->tune.vibrato = 1.0f; }
            if (p == 1) { t->tune.strength = 0.9f;  t->tune.retuneMs = 40;  t->tune.driftFix = 0.2f; t->tune.vibrato = 0.9f; }
            if (p == 2) { t->tune.strength = 1.0f;  t->tune.retuneMs = 0;   t->tune.driftFix = 1.0f; t->tune.vibrato = 0.0f; }
            ctx.session.changed();
        };

        auto bind = [this] (Knob& k, std::function<void (TuneSettings&, double)> set)
        {
            addAndMakeVisible (k);
            k.onDragStart = [this] { ctx.session.checkpoint ("Tuning"); };
            k.onChange = [this, set] (double v)
            {
                if (updating) return;
                if (auto* t = tuneOwner (ctx)) { set (t->tune, v); t->tune.preset = 3; ctx.session.changed(); }
            };
        };
        bind (strength, [] (TuneSettings& s, double v) { s.strength = (float) v; });
        bind (retune, [] (TuneSettings& s, double v) { s.retuneMs = (float) v; });
        bind (drift, [] (TuneSettings& s, double v) { s.driftFix = (float) v; });
        bind (vibrato, [] (TuneSettings& s, double v) { s.vibrato = (float) v; });

        auto bindVoice = [this] (Knob& k, std::function<void (HumanizeSettings&, double)> set)
        {
            addAndMakeVisible (k);
            k.onDragStart = [this] { ctx.session.checkpoint ("Voice character"); };
            k.onChange = [this, set] (double v)
            {
                if (updating) return;
                auto* t = ctx.selected();
                if (t != nullptr && t->isVoice()) { set (t->human, v); ctx.session.changed(); }
            };
        };
        bindVoice (variation, [] (HumanizeSettings& h, double v) { h.variation = (float) v; });
        bindVoice (other, [] (HumanizeSettings& h, double v) { h.otherSinger = (float) v; });
        bindVoice (belt, [] (HumanizeSettings& h, double v) { h.belt = (float) v; });

        styleButton (rerollBtn, "secondary");
        styleButton (addBtn, "primary");
        addAndMakeVisible (rerollBtn);
        addAndMakeVisible (addBtn);
        rerollBtn.onClick = [this] { if (auto* t = ctx.selected(); t && t->isVoice()) ctx.session.reroll (t->id); };
        addBtn.onClick = [this] { if (ctx.openAddVoices) ctx.openAddVoices(); };
    }

    juce::String title() const override { return "Tune"; }
    juce::String subtitle() const override { return "Find your pitch. Build your layers."; }
    juce::String help() const override
    {
        return "Key & scale: Auto reads it from the lead. Notes snap to this scale.\n\n"
               "Strength: how far notes move toward the target. Retune speed: how fast pitch glides between notes (0 ms = hard tune). "
               "Drift fix: steadies slow wandering inside a note. Vibrato: keeps, reduces or exaggerates your natural vibrato.\n\n"
               "Add voices builds doubles and harmonies from the selected take. Each voice is its own performance: "
               "Variation adds looseness, Other singer changes the tone like a different person, Belt pushes it brighter.\n\n"
               "Switch to Notes (top) to drag individual notes.";
    }

    void refresh() override
    {
        updating = true;
        auto& d = ctx.session.doc;
        keyCombo.changeItemText (1, "Auto (" + keyNames()[d.key] + ")");
        keyCombo.setSelectedId (d.autoKey ? 1 : d.key + 2, juce::dontSendNotification);
        scaleCombo.setSelectedId (d.scale + 1, juce::dontSendNotification);
        auto* t = tuneOwner (ctx);
        for (auto* k : { &strength, &retune, &drift, &vibrato }) k->setEnabled (t != nullptr);
        presetCombo.setEnabled (t != nullptr);
        if (t)
        {
            presetCombo.setSelectedId (t->tune.preset + 1, juce::dontSendNotification);
            strength.setValue (t->tune.strength); retune.setValue (t->tune.retuneMs);
            drift.setValue (t->tune.driftFix); vibrato.setValue (t->tune.vibrato);
        }
        auto* s = ctx.selected();
        const bool voice = s != nullptr && s->isVoice();
        for (auto* k : { &variation, &other, &belt }) k->setEnabled (voice);
        rerollBtn.setEnabled (voice);
        addBtn.setEnabled (d.lead() != nullptr);
        if (voice) { variation.setValue (s->human.variation); other.setValue (s->human.otherSinger); belt.setValue (s->human.belt); }
        updating = false;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        r.removeFromTop (24);
        auto row = r.removeFromTop (34);
        keyCombo.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
        row.removeFromLeft (8);
        scaleCombo.setBounds (row);
        r.removeFromTop (16);
        tuningY = r.getY();
        auto prow = r.removeFromTop (30);
        presetCombo.setBounds (prow.removeFromRight (140));
        r.removeFromTop (6);
        layoutKnobs (r, { &strength, &retune, &drift, &vibrato }, 4, 84);
        r.removeFromTop (14);
        voiceY = r.getY();
        auto vrow = r.removeFromTop (30);
        rerollBtn.setBounds (vrow.removeFromRight (110));
        r.removeFromTop (8);
        addBtn.setBounds (r.removeFromTop (40));
        r.removeFromTop (8);
        layoutKnobs (r, { &variation, &other, &belt }, 3, 84);
    }

    void paint (juce::Graphics& g) override
    {
        drawSectionLabel (g, "Key & scale", { 16, 4, 200, 18 });
        divider (g, tuningY - 8, getWidth());
        drawSectionLabel (g, "Tuning", { 16, tuningY + 6, 120, 18 });
        divider (g, voiceY - 8, getWidth());
        drawSectionLabel (g, "Selected voice", { 16, voiceY + 6, 160, 18 });
        auto* s = ctx.selected();
        if (s != nullptr && s->isVoice())
        {
            g.setColour (col::textFaint);
            g.setFont (Fonts::body (11.5f));
            g.drawText ("Tuning above belongs to the take this voice follows.", 16, tuningY + 22, getWidth() - 32, 14, juce::Justification::centredLeft);
        }
    }

    bool updating = false;
    int tuningY = 0, voiceY = 0;
    juce::ComboBox keyCombo, scaleCombo, presetCombo;
    Knob strength { "Strength", 0.0, 1.0, 1.0, formatPct };
    Knob retune { "Retune speed", 0.0, 400.0, 40.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; }, 80.0 };
    Knob drift { "Drift fix", 0.0, 1.0, 0.0, formatPct };
    Knob vibrato { "Vibrato", 0.0, 1.5, 1.0, formatPct };
    Knob variation { "Variation", 0.0, 1.0, 0.0, [] (double v) { return v < 0.01 ? juce::String ("off") : formatPct (v); } };
    Knob other { "Other singer", 0.0, 1.0, 0.0, [] (double v) { return v < 0.01 ? juce::String ("off") : formatPct (v); } };
    Knob belt { "Belt", 0.0, 1.0, 0.0, [] (double v) { return v < 0.01 ? juce::String ("off") : formatPct (v); } };
    juce::TextButton rerollBtn { "Reroll take" }, addBtn { "Add voices..." };
};

//==============================================================================
class TimePage : public Page
{
public:
    explicit TimePage (Context& c) : Page (c)
    {
        gridCombo.addItemList ({ "1/4", "1/8", "1/16", "1/32" }, 1);
        gridCombo.setSelectedId (3, juce::dontSendNotification);
        setupCombo (gridCombo);
        addAndMakeVisible (gridCombo);
        addAndMakeVisible (amount);
        addAndMakeVisible (tight);
        tight.setValue (0.85);
        ends.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (ends);

        styleButton (qTrack, "primary"); styleButton (qAll, "secondary"); styleButton (clear, "secondary");
        styleButton (aTrack, "secondary"); styleButton (aAll, "primary");
        for (auto* b : { &qTrack, &qAll, &clear, &aTrack, &aAll }) addAndMakeVisible (b);

        auto div = [this] { static const int d[] = { 4, 8, 16, 32 }; return d[juce::jlimit (0, 3, gridCombo.getSelectedId() - 1)]; };
        qTrack.onClick = [this, div] { if (auto* t = ctx.selected()) ctx.session.quantize (t->id, div(), (float) amount.getValue()); };
        qAll.onClick   = [this, div] { ctx.session.quantize (-1, div(), (float) amount.getValue()); };
        clear.onClick  = [this] { if (auto* t = ctx.selected()) ctx.session.clearWarps (t->id); };
        aTrack.onClick = [this] { if (auto* t = ctx.selected()) ctx.session.alignToLead (t->id, (float) tight.getValue(), ends.getToggleState()); };
        aAll.onClick   = [this] { ctx.session.alignToLead (-1, (float) tight.getValue(), ends.getToggleState()); };
        amount.setValue (0.5);
    }

    juce::String title() const override { return "Time"; }
    juce::String subtitle() const override { return "Lock your takes into the groove."; }
    juce::String help() const override
    {
        return "Quantize moves note starts toward the grid. 0% does nothing, 100% puts every note exactly on the grid; 40-60% tightens without sounding robotic.\n\n"
               "Align to lead lines takes and voices up with the lead. Tightness sets how close; Align note ends too also matches where notes stop.\n\n"
               "Warp markers (the amber flags) show where timing was changed. Drag a marker to move it, double-click a clip to add one, right-click a marker to remove it.";
    }

    void refresh() override
    {
        const bool has = ctx.selected() != nullptr;
        for (auto* b : { &qTrack, &clear, &aTrack }) b->setEnabled (has);
        auto* t = ctx.selected();
        aTrack.setEnabled (t != nullptr && ! t->isLead);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        r.removeFromTop (24);
        auto row = r.removeFromTop (86);
        amount.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (10);
        gridY = row.getY() + 6;
        gridCombo.setBounds (row.withTrimmedTop (26).withHeight (34));
        r.removeFromTop (8);
        auto b = r.removeFromTop (36);
        qTrack.setBounds (b.removeFromLeft (b.getWidth() / 2 - 4));
        b.removeFromLeft (8);
        qAll.setBounds (b);
        r.removeFromTop (8);
        clear.setBounds (r.removeFromTop (34));
        r.removeFromTop (22);
        alignY = r.getY();
        r.removeFromTop (24);
        auto arow = r.removeFromTop (86);
        tight.setBounds (arow.removeFromLeft (100));
        arow.removeFromLeft (10);
        ends.setBounds (arow.withSizeKeepingCentre (arow.getWidth(), 30));
        r.removeFromTop (8);
        aTrack.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        aAll.setBounds (r.removeFromTop (38));
    }

    void paint (juce::Graphics& g) override
    {
        drawSectionLabel (g, "Quantize", { 16, 4, 200, 18 });
        drawSectionLabel (g, "Grid", { gridCombo.getX(), gridY, 80, 16 }, col::textDim);
        divider (g, alignY - 10, getWidth());
        drawSectionLabel (g, "Align to lead", { 16, alignY, 200, 18 });
    }

    int gridY = 0, alignY = 0;
    Knob amount { "Quantize", 0.0, 1.0, 0.5, formatPct };
    Knob tight { "Tightness", 0.0, 1.0, 0.85, formatPct };
    juce::ComboBox gridCombo;
    juce::ToggleButton ends { "Align note ends too" };
    juce::TextButton qTrack { "Quantize track" }, qAll { "Quantize all" }, clear { "Clear warp markers" },
                     aTrack { "Align this track to lead" }, aAll { "Align all to lead" };
};

//==============================================================================
class StereoPanel : public juce::Component
{
public:
    explicit StereoPanel (Context& c) : ctx (c)
    {
        auto& s = ctx.session.doc.stereo;
        imager.setToggleState (s.imager, juce::dontSendNotification);
        stz.setToggleState (s.stereoize, juce::dontSendNotification);
        low.setValue (s.lowWidth); high.setValue (s.highWidth); amt.setValue (s.stereoizeAmt);
        for (auto* c2 : std::initializer_list<juce::Component*> { &imager, &stz, &low, &high, &amt }) addAndMakeVisible (c2);
        auto apply = [this]
        {
            auto& st = ctx.session.doc.stereo;
            st.imager = imager.getToggleState(); st.stereoize = stz.getToggleState();
            st.lowWidth = (float) low.getValue(); st.highWidth = (float) high.getValue(); st.stereoizeAmt = (float) amt.getValue();
            ctx.session.changed();
        };
        imager.onClick = [this, apply] { ctx.session.checkpoint ("Stereo"); apply(); };
        stz.onClick = [this, apply] { ctx.session.checkpoint ("Stereo"); apply(); };
        for (auto* k : { &low, &high, &amt }) { k->onDragStart = [this] { ctx.session.checkpoint ("Stereo"); }; k->onChange = [apply] (double) { apply(); }; }
        setSize (300, 210);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        r.removeFromTop (22);
        imager.setBounds (r.removeFromTop (28));
        auto row = r.removeFromTop (86);
        low.setBounds (row.removeFromLeft (row.getWidth() / 3));
        high.setBounds (row.removeFromLeft (row.getWidth() / 2));
        amt.setBounds (row);
        stz.setBounds (r.removeFromTop (28));
    }
    void paint (juce::Graphics& g) override
    {
        drawSectionLabel (g, "Stereo image", { 14, 10, 200, 18 });
    }
    Context& ctx;
    juce::ToggleButton imager { "Imager (whole stack)" }, stz { "Stereoize new voices" };
    Knob low { "Low width", 0.0, 2.0, 0.55, formatPct }, high { "High width", 0.0, 2.0, 1.1, formatPct },
         amt { "Stereoize", 0.0, 1.0, 0.05, formatPct };
};

class FinishPage : public Page
{
public:
    explicit FinishPage (Context& c) : Page (c)
    {
        for (int i = 0; i < Finish::chainNames().size(); ++i)
        {
            auto* b = chainBtns.add (new juce::TextButton (Finish::chainNames()[i]));
            styleButton (*b, "seg");
            b->onClick = [this, i]
            {
                ctx.session.checkpoint ("Chain " + Finish::chainNames()[i]);
                auto& f = target();
                f = Finish::preset (i);
                ctx.session.changed();
            };
            addAndMakeVisible (b);
        }
        enabled.onClick = [this] { ctx.session.checkpoint ("Finish on/off"); target().enabled = enabled.getToggleState(); ctx.session.changed(); };
        all.onClick = [this]
        {
            ctx.session.checkpoint ("Finish on all tracks");
            auto& d = ctx.session.doc;
            if (all.getToggleState()) { if (auto* t = ctx.selected()) d.finish = t->finish; }
            else for (auto& t : d.tracks) t.finish = d.finish;
            d.finishAllTracks = all.getToggleState();
            ctx.session.changed();
        };
        addAndMakeVisible (enabled);
        addAndMakeVisible (all);

        auto bind = [this] (Knob& k, std::function<void (FinishSettings&, double)> set)
        {
            addAndMakeVisible (k);
            k.onDragStart = [this] { ctx.session.checkpoint ("Finish"); };
            k.onChange = [this, set] (double v) { if (updating) return; set (target(), v); target().chain = -1; ctx.session.changed(); };
        };
        bind (lowCut, [] (FinishSettings& f, double v) { f.lowCutHz = (float) v; });
        bind (deplosive, [] (FinishSettings& f, double v) { f.deplosive = (float) v; });
        bind (mud, [] (FinishSettings& f, double v) { f.mudCutDb = (float) v; });
        bind (deess, [] (FinishSettings& f, double v) { f.deEsser = (float) v; });
        bind (presence, [] (FinishSettings& f, double v) { f.presenceDb = (float) v; });
        bind (air, [] (FinishSettings& f, double v) { f.airDb = (float) v; });
        bind (comp, [] (FinishSettings& f, double v) { f.compressDb = (float) v; });
        bind (ratio, [] (FinishSettings& f, double v) { f.ratio = (float) v; });
        presence.setBipolar (true); air.setBipolar (true);

        styleButton (stereoBtn, "secondary");
        addAndMakeVisible (stereoBtn);
        stereoBtn.onClick = [this]
        {
            juce::CallOutBox::launchAsynchronously (std::make_unique<StereoPanel> (ctx), stereoBtn.getScreenBounds(), nullptr);
        };
    }

    FinishSettings& target()
    {
        auto& d = ctx.session.doc;
        if (d.finishAllTracks) return d.finish;
        if (auto* t = ctx.selected()) return t->finish;
        return d.finish;
    }

    juce::String title() const override { return "Finish"; }
    juce::String subtitle() const override { return "Shape tone, dynamics and width."; }
    juce::String help() const override
    {
        return "A vocal chain on every track: low cut, de-plosive (soaks up P and B pops), mud cut, de-esser, presence, air and a compressor.\n\n"
               "Pick a starting chain, then adjust. Use on all tracks keeps every voice on one shared chain; turn it off to give the selected track its own.\n\n"
               "Stereo image widens the stack: lows stay focused, highs spread out.";
    }

    void refresh() override
    {
        updating = true;
        auto& f = target();
        for (int i = 0; i < chainBtns.size(); ++i) chainBtns[i]->setToggleState (f.chain == i, juce::dontSendNotification);
        enabled.setToggleState (f.enabled, juce::dontSendNotification);
        all.setToggleState (ctx.session.doc.finishAllTracks, juce::dontSendNotification);
        lowCut.setValue (f.lowCutHz); deplosive.setValue (f.deplosive); mud.setValue (f.mudCutDb); deess.setValue (f.deEsser);
        presence.setValue (f.presenceDb); air.setValue (f.airDb); comp.setValue (f.compressDb); ratio.setValue (f.ratio);
        for (auto* k : { &lowCut, &deplosive, &mud, &deess, &presence, &air, &comp, &ratio }) k->setEnabled (f.enabled);
        updating = false;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        r.removeFromTop (24);
        auto row = r.removeFromTop (32);
        const int w = row.getWidth() / juce::jmax (1, chainBtns.size());
        for (auto* b : chainBtns) b->setBounds (row.removeFromLeft (w).reduced (2, 0));
        r.removeFromTop (8);
        enabled.setBounds (r.removeFromTop (30));
        all.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        toneY = r.getY();
        r.removeFromTop (22);
        layoutKnobs (r, { &lowCut, &deplosive, &mud, &deess, &presence, &air, &comp, &ratio }, 4, 92);
        r.removeFromTop (14);
        stereoY = r.getY();
        r.removeFromTop (22);
        stereoBtn.setBounds (r.removeFromTop (34));
    }

    void paint (juce::Graphics& g) override
    {
        drawSectionLabel (g, "Chain", { 16, 4, 200, 18 });
        divider (g, toneY - 4, getWidth());
        drawSectionLabel (g, "Tone & dynamics", { 16, toneY + 2, 200, 18 });
        divider (g, stereoY - 4, getWidth());
        drawSectionLabel (g, "Stereo", { 16, stereoY + 2, 200, 18 });
    }

    bool updating = false;
    int toneY = 0, stereoY = 0;
    juce::OwnedArray<juce::TextButton> chainBtns;
    juce::ToggleButton enabled { "Finish enabled" }, all { "Use on all tracks" };
    Knob lowCut { "Low cut", 0.0, 300.0, 0.0, [] (double v) { return v < 1 ? juce::String ("auto") : juce::String (juce::roundToInt (v)) + " Hz"; } };
    Knob deplosive { "De-plosive", 0.0, 1.0, 0.6, formatPct };
    Knob mud { "Mud cut", -12.0, 0.0, -2.5, formatDb };
    Knob deess { "De-esser", 0.0, 1.0, 0.7, formatPct };
    Knob presence { "Presence", -6.0, 9.0, 2.0, formatDb };
    Knob air { "Air", -6.0, 9.0, 3.0, formatDb };
    Knob comp { "Compress", 0.0, 18.0, 6.0, formatDb };
    Knob ratio { "Ratio", 1.0, 10.0, 4.2, [] (double v) { return juce::String (v, 1) + ":1"; }, 4.0 };
    juce::TextButton stereoBtn { "Stereo image..." };
};

//==============================================================================
class LeftPanel::Transport : public juce::Component
{
public:
    explicit Transport (Context& c) : ctx (c)
    {
        play.framed = true;
        sync.text = "FL";
        sync.setClickingTogglesState (true);
        sync.setTooltip ("Follow FL's transport. Turn off to audition here while FL is stopped.");
        play.setTooltip ("Play / stop (here)");
        play.onClick = [this] { ctx.proc.setInternalPlaying (! ctx.proc.isInternalPlaying()); };
        sync.onClick = [this] { ctx.proc.followHost.store (sync.getToggleState()); };
        addAndMakeVisible (play);
        addAndMakeVisible (sync);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        play.setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 34));
        r.removeFromLeft (8);
        sync.setBounds (r.removeFromLeft (64).withSizeKeepingCentre (64, 30));
    }
    void paint (juce::Graphics& g) override
    {
        play.setIcon (ctx.proc.isInternalPlaying() ? Icons::Id::stop : Icons::Id::play);
        sync.setToggleState (ctx.proc.followHost.load(), juce::dontSendNotification);
        sync.setIcon (ctx.proc.isHostPlaying() ? Icons::Id::stop : Icons::Id::play);

        const double pos = ctx.proc.positionSeconds();
        const double beatLen = 60.0 / juce::jmax (20.0, ctx.session.doc.bpm);
        const auto beats = (juce::int64) std::floor (pos / beatLen + 1e-6);
        const int bpb = ctx.session.doc.beatsPerBar;
        const auto bar = beats / bpb + 1;
        const auto beat = beats % bpb + 1;
        g.setColour (col::text);
        g.setFont (Fonts::mono (15.0f));
        g.drawText ("Bar " + juce::String (bar) + "  Beat " + juce::String (beat), 116, 0, getWidth() - 140, getHeight(), juce::Justification::centredLeft);

        const bool busy = ctx.session.busyCount() > 0;
        const float a = busy ? 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() / 160.0f) : 1.0f;
        g.setColour ((busy ? col::amber : (ctx.session.doc.tracks.empty() ? col::textFaint : col::good)).withAlpha (a));
        g.fillEllipse ((float) getWidth() - 14.0f, getHeight() * 0.5f - 5.0f, 10.0f, 10.0f);
    }
    Context& ctx;
    IconButton play { Icons::Id::play, "Play" }, sync { Icons::Id::play, "Follow FL" };
};

//==============================================================================
class LeftPanel::DragHandle : public juce::Component, public juce::DragAndDropContainer, public juce::SettableTooltipClient
{
public:
    explicit DragHandle (Context& c) : ctx (c)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag onto FL's playlist at bar 1. Right-click for options.");
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (hover ? col::raisedHi : col::raised);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (hover ? col::ember.withAlpha (0.6f) : col::border);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        Icons::draw (g, Icons::Id::grip, r.removeFromLeft (34).withSizeKeepingCentre (18, 18), col::textDim);
        const bool busy = ctx.session.busyCount() > 0;
        g.setColour (col::text);
        g.setFont (Fonts::semi (13.5f));
        g.drawText (busy ? "Preparing vocals..." : (asMix ? "Drag mix to playlist" : "Drag to playlist"), r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::bottomLeft);
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.0f));
        g.drawText (asMix ? "one stereo file, drop at bar 1" : "one file per track, drop at bar 1", r, juce::Justification::topLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Drag one file per track", true, ! asMix);
            m.addItem (2, "Drag one mixed file", true, asMix);
            m.addSeparator();
            m.addItem (3, "Export now and show the files");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r == 1) asMix = false;
                if (r == 2) asMix = true;
                if (r == 3)
                {
                    auto files = ctx.session.exportForDrag (! asMix);
                    if (! files.isEmpty()) files[0].revealToUser();
                }
                repaint();
            });
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragged || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6) return;
        dragged = true;
        if (ctx.session.busyCount() > 0)
        {
            if (ctx.toast) ctx.toast ("Still rendering - try again in a moment.");
            return;
        }
        auto files = ctx.session.exportForDrag (! asMix);
        if (files.isEmpty()) { if (ctx.toast) ctx.toast ("Nothing to drag yet."); return; }
        juce::StringArray paths;
        for (auto& f : files) paths.add (f.getFullPathName());
        performExternalDragDropOfFiles (paths, false, this);
    }
    Context& ctx;
    bool hover = false, dragged = false, asMix = false;
};

//==============================================================================
LeftPanel::LeftPanel (Context& c) : ctx (c)
{
    setupCombo (trackCombo);
    addAndMakeVisible (trackCombo);
    trackCombo.onChange = [this]
    {
        const int i = trackCombo.getSelectedItemIndex();
        if (i >= 0 && i < (int) comboIds.size()) ctx.select (comboIds[(size_t) i]);
    };
    addAndMakeVisible (titleBar);

    pages.push_back (std::make_unique<ImportPage> (ctx));
    pages.push_back (std::make_unique<CleanPage> (ctx));
    pages.push_back (std::make_unique<TunePage> (ctx));
    pages.push_back (std::make_unique<TimePage> (ctx));
    pages.push_back (std::make_unique<FinishPage> (ctx));
    for (auto& p : pages) addChildComponent (*p);

    transport = std::make_unique<Transport> (ctx);
    dragHandle = std::make_unique<DragHandle> (ctx);
    addAndMakeVisible (*transport);
    addAndMakeVisible (*dragHandle);

    outputCombo.addItemList ({ "Replace insert vocals", "Blend with insert vocals", "Input only (bypass)" }, 1);
    setupCombo (outputCombo);
    outputCombo.onChange = [this]
    {
        ctx.session.doc.outputMode = (OutputMode) (outputCombo.getSelectedId() - 1);
        ctx.session.changed();
    };
    outputCombo.setTooltip ("What this insert outputs: the tuned stack instead of the raw vocal, both together, or the raw vocal only.");
    addAndMakeVisible (outputCombo);

    for (auto* b : { &monoBtn, &undoBtn, &redoBtn, &historyBtn }) { styleButton (*b, "secondary"); addAndMakeVisible (b); }
    monoBtn.setClickingTogglesState (true);
    monoBtn.setTooltip ("Check the stack in mono");
    monoBtn.onClick = [this] { ctx.session.doc.mono = monoBtn.getToggleState(); ctx.session.changed(); };
    undoBtn.onClick = [this] { ctx.session.undo(); };
    redoBtn.onClick = [this] { ctx.session.redo(); };
    historyBtn.onClick = [this]
    {
        juce::PopupMenu m;
        auto labels = ctx.session.historyLabels();
        const int pos = ctx.session.historyPosition();
        m.addSectionHeader ("History");
        for (int i = juce::jmax (0, labels.size() - 30); i < labels.size(); ++i)
            m.addItem (i + 1, labels[i], true, i == pos);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyBtn), [this] (int r)
        {
            if (r > 0) ctx.session.jumpToHistory (r - 1);
        });
    };

    titleBar.onHelp = [this]
    {
        auto& p = pages[(size_t) juce::jlimit (0, 4, ctx.ui.page)];
        if (ctx.showHelp) ctx.showHelp (p->title(), p->help(), &titleBar);
    };

    ctx.session.addChangeListener (this);
    ctx.ui.addChangeListener (this);
    startTimerHz (15);
    showPage (ctx.ui.page);
}

LeftPanel::~LeftPanel()
{
    ctx.session.removeChangeListener (this);
    ctx.ui.removeChangeListener (this);
}

void LeftPanel::showPage (int page)
{
    for (int i = 0; i < (int) pages.size(); ++i) pages[(size_t) i]->setVisible (i == page);
    auto& p = pages[(size_t) juce::jlimit (0, 4, page)];
    titleBar.title = p->title();
    titleBar.subtitle = p->subtitle();
    titleBar.repaint();
    p->refresh();
    resized();
}

void LeftPanel::refreshTrackCombo()
{
    std::vector<int> ids;
    for (auto& t : ctx.session.doc.tracks) ids.push_back (t.id);
    if (ids != comboIds)
    {
        comboIds = ids;
        trackCombo.clear (juce::dontSendNotification);
        int k = 1;
        for (auto& t : ctx.session.doc.tracks) trackCombo.addItem (t.name + (t.isLead ? "  [LEAD]" : ""), k++);
    }
    else
    {
        int k = 1;
        for (auto& t : ctx.session.doc.tracks) trackCombo.changeItemText (k++, t.name + (t.isLead ? "  [LEAD]" : ""));
    }
    auto* sel = ctx.selected();
    trackCombo.setSelectedItemIndex (sel ? ctx.session.doc.indexOf (sel->id) : -1, juce::dontSendNotification);
    trackCombo.setTextWhenNothingSelected ("No tracks yet");
}

void LeftPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshTrackCombo();
    pages[(size_t) juce::jlimit (0, 4, ctx.ui.page)]->refresh();
    outputCombo.setSelectedId ((int) ctx.session.doc.outputMode + 1, juce::dontSendNotification);
    monoBtn.setToggleState (ctx.session.doc.mono, juce::dontSendNotification);
    undoBtn.setEnabled (ctx.session.canUndo());
    redoBtn.setEnabled (ctx.session.canRedo());
    repaint();
}

void LeftPanel::timerCallback()
{
    transport->repaint();
    dragHandle->repaint();
    repaint (0, transport->getBottom(), getWidth(), 22);
}

void LeftPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (78).reduced (16, 10);
    top.removeFromTop (18);
    trackCombo.setBounds (top.withTrimmedLeft (4));
    titleBar.setBounds (r.removeFromTop (60).reduced (16, 4));

    auto bottom = r.removeFromBottom (214).reduced (16, 10);
    transport->setBounds (bottom.removeFromTop (40));
    bottom.removeFromTop (22);
    dragHandle->setBounds (bottom.removeFromTop (48));
    bottom.removeFromTop (8);
    auto orow = bottom.removeFromTop (34);
    monoBtn.setBounds (orow.removeFromRight (64));
    orow.removeFromRight (8);
    outputCombo.setBounds (orow);
    bottom.removeFromTop (8);
    auto urow = bottom.removeFromTop (32);
    const int w = (urow.getWidth() - 16) / 3;
    undoBtn.setBounds (urow.removeFromLeft (w)); urow.removeFromLeft (8);
    redoBtn.setBounds (urow.removeFromLeft (w)); urow.removeFromLeft (8);
    historyBtn.setBounds (urow);

    for (auto& p : pages) p->setBounds (r);
}

void LeftPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::border);
    g.drawVerticalLine (getWidth() - 1, 0, (float) getHeight());

    drawSectionLabel (g, "Selected track", { 20, 8, 200, 18 }, col::ember.withAlpha (0.85f));
    if (auto* t = ctx.selected())
    {
        g.setColour (t->colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, (float) trackCombo.getY() + 6, 3.0f, (float) trackCombo.getHeight() - 12), 1.5f);
    }

    // bottom block
    const int by = transport->getY() - 10;
    g.setColour (col::border);
    g.drawHorizontalLine (by, 0, (float) getWidth());
    g.setColour (col::textFaint);
    g.setFont (Fonts::medium (12.5f));
    auto& d = ctx.session.doc;
    juce::String info = juce::String (d.bpm, d.bpm == std::floor (d.bpm) ? 0 : 1) + " BPM  " + dot + "  " + keyNames()[d.key] + " " + scaleNames()[d.scale];
    const int busy = ctx.session.busyCount();
    if (busy > 0) info = "Processing " + juce::String (busy) + (busy == 1 ? " track..." : " tracks...");
    g.setColour (busy > 0 ? col::amber : col::textFaint);
    g.drawText (info, 16, transport->getBottom() + 2, getWidth() - 32, 18, juce::Justification::centredLeft);
}
} // namespace hvx::ui
