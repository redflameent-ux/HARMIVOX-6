#include "AddVoicesDialog.h"

namespace hvx::ui
{
//==============================================================================
class AddVoicesDialog::VoiceRow : public juce::Component
{
public:
    VoiceRow (AddVoicesDialog& d, int i) : dlg (d), index (i)
    {
        for (auto* b : { &minus, &plus }) { b->framed = true; addAndMakeVisible (b); }
        minus.onClick = [this] { auto& v = dlg.voices[(size_t) index]; v.takes = juce::jmax (1, v.takes - 1); dlg.refreshButtons(); repaint(); };
        plus.onClick  = [this] { auto& v = dlg.voices[(size_t) index]; v.takes = juce::jmin (6, v.takes + 1); dlg.refreshButtons(); repaint(); };
        lvl.setSliderStyle (juce::Slider::LinearHorizontal);
        lvl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        lvl.setRange (-24.0, 6.0);
        lvl.setValue (dlg.voices[(size_t) index].levelDb, juce::dontSendNotification);
        lvl.setDoubleClickReturnValue (true, -6.0);
        lvl.onValueChange = [this] { dlg.voices[(size_t) index].levelDb = (float) lvl.getValue(); repaint(); };
        addAndMakeVisible (lvl);
        remove.framed = false;
        remove.onClick = [this] { dlg.voices.erase (dlg.voices.begin() + index); dlg.rebuildRows(); };
        addAndMakeVisible (remove);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        remove.setBounds (r.removeFromRight (26).withSizeKeepingCentre (22, 22));
        r.removeFromLeft (122);
        auto t = r.removeFromLeft (84);
        minus.setBounds (t.removeFromLeft (24).withSizeKeepingCentre (24, 24));
        plus.setBounds (t.removeFromRight (24).withSizeKeepingCentre (24, 24));
        r.removeFromLeft (8);
        lvl.setBounds (r.removeFromLeft (88).withSizeKeepingCentre (88, 22));
        panArea = r.withTrimmedLeft (58).reduced (8, 0);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& v = dlg.voices[(size_t) index];
        g.setColour (col::border.withAlpha (0.6f));
        g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
        g.setColour (col::text);
        g.setFont (Fonts::semi (14.0f));
        juce::String name = v.steps == 0 ? "Double" : Harmony::intervalLabels()[juce::jlimit (0, 9, std::abs (v.steps))] + (v.steps > 0 ? " above" : " below");
        g.drawText (name, 0, 0, 120, getHeight(), juce::Justification::centredLeft);
        g.setColour (col::text);
        g.setFont (Fonts::mono (14.0f));
        g.drawText (juce::String (v.takes), 122 + 24, 0, 36, getHeight(), juce::Justification::centred);
        g.setColour (col::textDim);
        g.setFont (Fonts::medium (12.5f));
        g.drawText (juce::String (v.levelDb, 1) + " dB", lvl.getRight() + 4, 0, 54, getHeight(), juce::Justification::centredLeft);

        // pan preview
        auto pr = panArea.toFloat().withSizeKeepingCentre ((float) panArea.getWidth(), 2.0f);
        g.setColour (col::border);
        g.fillRect (pr);
        g.fillRect (juce::Rectangle<float> (pr.getCentreX() - 0.5f, pr.getY() - 4, 1, 10));
        const float sp = (float) dlg.spread.getValue();
        for (int k = 0; k < v.takes; ++k)
        {
            float p;
            if (v.takes == 1) p = 0.0f;
            else if (v.takes == 2) p = k == 0 ? -0.9f : 0.9f;
            else { static const float q[] = { -0.55f, 0.55f, -1.0f, 1.0f, -0.3f, 0.3f }; p = q[k % 6]; }
            p *= sp;
            const float x = pr.getCentreX() + p * pr.getWidth() * 0.5f;
            g.setGradientFill (accentGradient (pr));
            g.fillEllipse (x - 5, pr.getCentreY() - 5, 10, 10);
        }
    }
    AddVoicesDialog& dlg;
    const int index;
    IconButton minus { Icons::Id::minus, "Fewer takes" }, plus { Icons::Id::plus, "More takes" }, remove { Icons::Id::cross, "Remove" };
    juce::Slider lvl;
    juce::Rectangle<int> panArea;
};

//==============================================================================
AddVoicesDialog::AddVoicesDialog (Context& c) : ctx (c)
{
    setWantsKeyboardFocus (true);

    for (auto& p : Harmony::presets())
    {
        auto* b = presetBtns.add (new juce::TextButton (p.name));
        styleButton (*b, "seg");
        b->onClick = [this, &p] { applyPreset (p); };
        addAndMakeVisible (b);
    }
    for (auto& p : Harmony::ideas())
    {
        auto* b = ideaBtns.add (new juce::TextButton (p.name));
        styleButton (*b, "seg");
        b->onClick = [this, &p] { applyPreset (p); };
        addAndMakeVisible (b);
    }
    for (int s = 0; s < 10; ++s)
    {
        auto* a = aboveBtns.add (new juce::TextButton (Harmony::intervalLabels()[s]));
        styleButton (*a, "seg");
        a->onClick = [this, s] { toggleInterval (s); };
        addAndMakeVisible (a);
        auto* b = belowBtns.add (new juce::TextButton (Harmony::intervalLabels()[s]));
        styleButton (*b, "seg");
        b->onClick = [this, s] { toggleInterval (-s); };
        b->setVisible (s > 0);
        addChildComponent (b);
        if (s > 0) b->setVisible (true);
    }
    for (auto name : { "Follow key", "Follow chords", "Exact" })
    {
        auto* b = modeBtns.add (new juce::TextButton (name));
        styleButton (*b, "seg");
        addAndMakeVisible (b);
    }
    modeBtns[0]->setTooltip ("Harmonies stay inside the song's key and scale.");
    modeBtns[1]->setTooltip ("Harmonies land on notes of the chord lane (set on the Tune page).");
    modeBtns[2]->setTooltip ("Fixed intervals in semitones, like a pitch shifter.");
    for (int i = 0; i < 3; ++i) modeBtns[i]->onClick = [this, i] { mode = (IntervalMode) i; refreshButtons(); };

    for (auto* k : { &loose, &others, &spread, &level, &lowW, &highW, &stz }) addAndMakeVisible (k);
    spread.onChange = [this] (double) { for (auto* r : rows) r->repaint(); };
    auto& st = ctx.session.doc.stereo;
    lowW.setValue (st.lowWidth); highW.setValue (st.highWidth); stz.setValue (st.stereoizeAmt);
    imager.setToggleState (st.imager, juce::dontSendNotification);
    stzOn.setToggleState (st.stereoize, juce::dontSendNotification);
    const bool hasProfile = ctx.session.profile() != nullptr && ctx.session.profile()->valid;
    useProfile.setToggleState (hasProfile, juce::dontSendNotification);
    useProfile.setEnabled (hasProfile);
    useProfile.setTooltip (hasProfile ? "New voices keep the tone of your voice as they move up or down."
                                      : "Make a voice in the Voice lab first.");
    for (auto* t : { &imager, &stzOn, &useProfile }) addAndMakeVisible (t);

    setupCombo (fromCombo);
    int k = 1;
    for (auto& t : ctx.session.doc.tracks)
        if (! t.isVoice()) { fromCombo.addItem (t.name, k++); fromIds.push_back (t.id); }
    int pick = 0;
    if (auto* s = ctx.selected())
    {
        const int want = s->isVoice() ? s->voice.parentId : s->id;
        for (size_t i = 0; i < fromIds.size(); ++i) if (fromIds[i] == want) pick = (int) i;
    }
    fromCombo.setSelectedItemIndex (pick, juce::dontSendNotification);
    addAndMakeVisible (fromCombo);

    styleButton (cancelBtn, "secondary");
    styleButton (createBtn, "primary");
    cancelBtn.onClick = [this] { if (onClose) onClose(); };
    createBtn.onClick = [this] { create(); };
    addAndMakeVisible (cancelBtn);
    addAndMakeVisible (createBtn);

    applyPreset (Harmony::presets().front());
}

AddVoicesDialog::~AddVoicesDialog() = default;

juce::Rectangle<int> AddVoicesDialog::card() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (960, getWidth() - 40), juce::jmin (720, getHeight() - 30));
}

void AddVoicesDialog::applyPreset (const HarmonyPreset& p)
{
    voices = p.voices;
    mode = p.mode;
    presetName = p.name;
    presetDesc = p.description;
    loose.setValue (p.looseness);
    rebuildRows();
}

void AddVoicesDialog::toggleInterval (int steps)
{
    auto it = std::find_if (voices.begin(), voices.end(), [steps] (auto& v) { return v.steps == steps; });
    if (it != voices.end()) voices.erase (it);
    else voices.push_back ({ steps, steps == 0 ? 2 : 1, steps == 0 ? -4.0f : -6.0f - 0.5f * (float) std::abs (steps) });
    std::sort (voices.begin(), voices.end(), [] (auto& a, auto& b) { return a.steps > b.steps; });
    presetName = "Custom";
    presetDesc = "Your own stack.";
    rebuildRows();
}

void AddVoicesDialog::rebuildRows()
{
    rows.clear();
    for (int i = 0; i < (int) voices.size(); ++i) addAndMakeVisible (rows.add (new VoiceRow (*this, i)));
    refreshButtons();
    resized();
    repaint();
}

void AddVoicesDialog::refreshButtons()
{
    for (int i = 0; i < presetBtns.size(); ++i) presetBtns[i]->setToggleState (presetName == Harmony::presets()[(size_t) i].name, juce::dontSendNotification);
    for (int i = 0; i < ideaBtns.size(); ++i) ideaBtns[i]->setToggleState (presetName == Harmony::ideas()[(size_t) i].name, juce::dontSendNotification);
    for (int s = 0; s < 10; ++s)
    {
        auto has = [this] (int st) { return std::any_of (voices.begin(), voices.end(), [st] (auto& v) { return v.steps == st; }); };
        aboveBtns[s]->setToggleState (has (s), juce::dontSendNotification);
        belowBtns[s]->setToggleState (s > 0 && has (-s), juce::dontSendNotification);
    }
    for (int i = 0; i < 3; ++i) modeBtns[i]->setToggleState ((int) mode == i, juce::dontSendNotification);
    int n = 0;
    for (auto& v : voices) n += v.takes;
    createBtn.setButtonText (n == 0 ? juce::String ("Create voices") : "Create " + juce::String (n) + (n == 1 ? " voice" : " voices"));
    createBtn.setEnabled (n > 0 && ! fromIds.empty());
    repaint();
}

void AddVoicesDialog::create()
{
    if (fromIds.empty() || voices.empty()) return;
    ctx.session.checkpoint ("Add voices: " + presetName);
    auto& st = ctx.session.doc.stereo;
    st.imager = imager.getToggleState();
    st.lowWidth = (float) lowW.getValue(); st.highWidth = (float) highW.getValue();
    st.stereoize = stzOn.getToggleState(); st.stereoizeAmt = (float) stz.getValue();

    AddVoicesOptions o;
    o.parentId = fromIds[(size_t) juce::jmax (0, fromCombo.getSelectedItemIndex())];
    o.voices = voices;
    o.mode = mode;
    o.looseness = (float) loose.getValue();
    o.otherSingers = (float) others.getValue();
    o.panSpread = (float) spread.getValue();
    o.levelDb = (float) level.getValue();
    o.useProfile = useProfile.getToggleState();
    auto ids = ctx.session.addVoices (o);
    if (! ids.empty()) ctx.select (ids.front());
    if (ctx.toast) ctx.toast (juce::String ((int) ids.size()) + " voices created - rendering now");
    if (onClose) onClose();
}

//==============================================================================
void AddVoicesDialog::resized()
{
    auto c = card().reduced (28, 24);
    auto header = c.removeFromTop (58);
    fromCombo.setBounds (header.removeFromRight (300).withSizeKeepingCentre (300, 34).withY (header.getY() + 22));

    auto top = c.removeFromTop (312);
    auto leftCol = top.removeFromLeft (380);
    top.removeFromLeft (24);

    leftCol.removeFromTop (22);
    auto grid = [] (juce::Rectangle<int>& r, juce::OwnedArray<juce::TextButton>& bs)
    {
        for (int i = 0; i < bs.size(); i += 2)
        {
            auto row = r.removeFromTop (32);
            bs[i]->setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
            row.removeFromLeft (8);
            if (i + 1 < bs.size()) bs[i + 1]->setBounds (row);
            r.removeFromTop (6);
        }
    };
    grid (leftCol, presetBtns);
    leftCol.removeFromTop (16);
    grid (leftCol, ideaBtns);

    listArea = top;
    auto la = top.reduced (18, 14);
    la.removeFromTop (78);
    for (auto* r : rows) r->setBounds (la.removeFromTop (38));

    auto foot = c.removeFromBottom (44);
    createBtn.setBounds (foot.removeFromRight (190));
    foot.removeFromRight (10);
    cancelBtn.setBounds (foot.removeFromRight (120));
    useProfile.setBounds (foot.removeFromLeft (300).withSizeKeepingCentre (300, 30));
    c.removeFromBottom (10);

    c.removeFromTop (12);
    auto ivHead = c.removeFromTop (30);
    for (int i = 2; i >= 0; --i) { modeBtns[i]->setBounds (ivHead.removeFromRight (118)); ivHead.removeFromRight (8); }
    c.removeFromTop (8);
    auto rowA = c.removeFromTop (34).withTrimmedLeft (70);
    auto rowB = (c.removeFromTop (42).withTrimmedTop (8)).withTrimmedLeft (70);
    const int w = rowA.getWidth() / 10;
    for (int s = 0; s < 10; ++s)
    {
        aboveBtns[s]->setBounds (rowA.removeFromLeft (w).reduced (3, 0));
        belowBtns[s]->setBounds (rowB.removeFromLeft (w).reduced (3, 0));
    }

    c.removeFromTop (18);
    auto perf = c.removeFromTop (110);
    auto pl = perf.removeFromLeft (perf.getWidth() / 2).withTrimmedTop (22);
    for (auto* k : { &loose, &others, &spread, &level }) k->setBounds (pl.removeFromLeft (pl.getWidth() / (k == &level ? 1 : (k == &spread ? 2 : (k == &others ? 3 : 4)))));
    perf.removeFromLeft (24);
    auto stHead = perf.removeFromTop (22);
    stzOn.setBounds (stHead.removeFromRight (120));
    stHead.removeFromRight (12);
    imager.setBounds (stHead.removeFromRight (100));
    for (auto* k : { &lowW, &highW, &stz }) k->setBounds (perf.removeFromLeft (perf.getWidth() / (k == &stz ? 1 : (k == &highW ? 2 : 3))));

}

void AddVoicesDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    auto c = card().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (c, 14.0f);
    g.setColour (col::borderHi);
    g.drawRoundedRectangle (c.reduced (0.5f), 14.0f, 1.0f);

    auto in = card().reduced (28, 24);
    g.setColour (col::text);
    g.setFont (Fonts::display (28.0f));
    g.drawText ("Add voices", in.getX(), in.getY(), 400, 30, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (13.5f));
    g.drawText ("Doubles, harmonies and full stacks. Every new voice is its own performance.", in.getX(), in.getY() + 30, 560, 20, juce::Justification::centredLeft);
    drawSectionLabel (g, "From", { fromCombo.getX(), fromCombo.getY() - 20, 100, 18 });

    if (! presetBtns.isEmpty()) drawSectionLabel (g, "Presets", { presetBtns[0]->getX(), presetBtns[0]->getY() - 22, 200, 18 });
    if (! ideaBtns.isEmpty()) drawSectionLabel (g, "Harmony ideas", { ideaBtns[0]->getX(), ideaBtns[0]->getY() - 22, 200, 18 });

    // summary panel
    auto la = listArea.toFloat();
    g.setColour (col::bg.withAlpha (0.7f));
    g.fillRoundedRectangle (la, 10.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (la, 10.0f, 1.0f);
    auto lr = listArea.reduced (18, 14);
    g.setColour (col::text);
    g.setFont (Fonts::displaySemi (19.0f));
    g.drawText (presetName, lr.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (13.0f));
    g.drawFittedText (presetDesc, lr.removeFromTop (34), juce::Justification::topLeft, 2);
    auto hdr = lr.removeFromTop (20);
    drawSectionLabel (g, "Voice", hdr.withWidth (140));
    drawSectionLabel (g, "Takes", hdr.withTrimmedLeft (122).withWidth (84), col::textFaint);
    drawSectionLabel (g, "Level", hdr.withTrimmedLeft (214).withWidth (100));
    if (! rows.isEmpty())
    {
        auto pa = rows[0]->panArea.translated (rows[0]->getX(), 0);
        drawSectionLabel (g, "L", { pa.getX() - 2, hdr.getY(), 12, hdr.getHeight() });
        drawSectionLabel (g, "R", { pa.getRight() - 8, hdr.getY(), 12, hdr.getHeight() });
    }
    if (voices.empty())
    {
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (13.5f));
        g.drawText ("No voices selected.", lr.removeFromTop (40), juce::Justification::centredLeft);
    }

    // intervals
    if (! aboveBtns.isEmpty())
    {
        auto a = aboveBtns[0]->getBounds();
        drawSectionLabel (g, "Intervals", { in.getX(), modeBtns[0]->getY(), 120, modeBtns[0]->getHeight() });
        g.setColour (col::amber);
        g.fillEllipse ((float) in.getX() + 94, (float) modeBtns[0]->getBounds().getCentreY() - 3.0f, 6, 6);
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.0f));
        g.drawText ("spicy", in.getX() + 104, modeBtns[0]->getY(), 60, modeBtns[0]->getHeight(), juce::Justification::centredLeft);
        drawSectionLabel (g, "Above", { in.getX(), a.getY(), 70, a.getHeight() });
        drawSectionLabel (g, "Below", { in.getX(), belowBtns[1]->getY(), 70, a.getHeight() });
        for (int s = 0; s < 10; ++s)
            if (Harmony::isSpicy (s))
                for (auto* b : { aboveBtns[s], belowBtns[s] })
                {
                    g.setColour (col::amber);
                    g.fillEllipse ((float) b->getRight() - 10.0f, (float) b->getY() + 4.0f, 5.0f, 5.0f);
                }
    }
    g.setColour (col::border);
    const int perfY = loose.getY() - 30;
    g.drawHorizontalLine (perfY, (float) in.getX(), (float) in.getRight());
    drawSectionLabel (g, "Performance", { in.getX(), perfY + 8, 200, 18 });
    drawSectionLabel (g, "Stereo", { lowW.getX(), perfY + 8, 120, 18 });
}

void AddVoicesDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition()) && onClose) onClose();
}

bool AddVoicesDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == juce::KeyPress::returnKey) { create(); return true; }
    return false;
}
} // namespace hvx::ui
