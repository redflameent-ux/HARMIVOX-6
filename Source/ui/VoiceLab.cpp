#include "VoiceLab.h"
#include "../engine/Analysis.h"

namespace hvx::ui
{
//==============================================================================
VoiceLabPicker::VoiceLabPicker (Context& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    styleButton (startBtn, "primary");
    styleButton (closeBtn, "secondary");
    styleButton (deleteBtn, "ghost");
    startBtn.onClick = [this] { if (onStartSession) onStartSession(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    deleteBtn.onClick = [this]
    {
        const auto cur = ctx.session.profileFileName;
        if (cur.isEmpty()) return;
        auto f = VoiceProfile::folder().getChildFile (cur);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete voice",
            "Delete \"" + f.getFileNameWithoutExtension() + "\"? This cannot be undone.", "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create ([this, f] (int r)
            {
                if (r != 1) return;
                f.deleteFile();
                ctx.session.profileFileName.clear();
                ctx.session.setProfile (nullptr);
                reload();
            }));
    };
    for (auto* b : { &startBtn, &closeBtn, &deleteBtn }) addAndMakeVisible (b);
    reload();
}

void VoiceLabPicker::reload()
{
    files = VoiceProfile::listSaved();
    deleteBtn.setVisible (ctx.session.profileFileName.isNotEmpty());
    repaint();
}

juce::Rectangle<int> VoiceLabPicker::card() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (760, getWidth() - 40), juce::jmin (560, getHeight() - 40));
}

juce::Rectangle<int> VoiceLabPicker::rowRect (int i) const
{
    return listArea.withHeight (52).translated (0, i * 60);
}

void VoiceLabPicker::resized()
{
    auto c = card().reduced (28, 24);
    c.removeFromTop (64);
    auto hero = c.removeFromTop (124);
    startBtn.setBounds (hero.reduced (24, 0).removeFromRight (210).withSizeKeepingCentre (210, 42));
    c.removeFromTop (40);
    auto foot = c.removeFromBottom (44);
    closeBtn.setBounds (foot.removeFromRight (120));
    deleteBtn.setBounds (foot.removeFromLeft (110));
    listArea = c;
}

void VoiceLabPicker::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    auto cf = card().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (cf, 14.0f);
    g.setColour (col::borderHi);
    g.drawRoundedRectangle (cf.reduced (0.5f), 14.0f, 1.0f);

    auto c = card().reduced (28, 24);
    g.setColour (col::text);
    g.setFont (Fonts::display (28.0f));
    g.drawText ("Voice lab", c.removeFromTop (32), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (13.5f));
    g.drawText ("Teach HARMIVOX-6 your voice, then every new double and harmony keeps your tone.", c.removeFromTop (22), juce::Justification::centredLeft);
    c.removeFromTop (10);

    auto hero = c.removeFromTop (124).toFloat();
    g.setColour (col::raised);
    g.fillRoundedRectangle (hero, 12.0f);
    auto icon = juce::Rectangle<float> (hero.getX() + 24, hero.getCentreY() - 28, 56, 56);
    g.setGradientFill (accentGradient (icon));
    g.fillEllipse (icon);
    Icons::draw (g, Icons::Id::mic, icon.reduced (14), juce::Colour (0xff1a0b0b), 2.0f);
    auto txt = hero.withTrimmedLeft (100).withTrimmedRight (250).reduced (0, 18);
    g.setColour (col::text);
    g.setFont (Fonts::semi (17.0f));
    g.drawText ("Create your voice", txt.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (13.0f));
    g.drawFittedText ("About 10 minutes with headphones on. 20 short phrases, each one checked. "
                      "Everything stays on this computer.", txt.toNearestInt(), juce::Justification::topLeft, 3);

    drawSectionLabel (g, "Your voices", { listArea.getX(), listArea.getY() - 26, 200, 18 });
    const auto cur = ctx.session.profileFileName;
    auto drawRow = [&] (int i, const juce::String& name, const juce::String& sub, bool inUse)
    {
        auto r = rowRect (i).toFloat();
        g.setColour (inUse ? col::raisedHi : col::raised);
        g.fillRoundedRectangle (r, 10.0f);
        if (inUse) { g.setGradientFill (accentGradient (r, 0.8f)); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f); }
        else { g.setColour (col::border); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f); }
        auto radio = juce::Rectangle<float> (r.getX() + 16, r.getCentreY() - 8, 16, 16);
        g.setColour (col::borderHi);
        g.drawEllipse (radio, 1.4f);
        if (inUse) { g.setGradientFill (accentGradient (radio)); g.fillEllipse (radio.reduced (3.5f)); }
        g.setColour (col::text);
        g.setFont (Fonts::semi (14.5f));
        g.drawText (name, r.withTrimmedLeft (46).withTrimmedBottom (r.getHeight() * 0.45f), juce::Justification::bottomLeft);
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.5f));
        g.drawText (sub, r.withTrimmedLeft (46).withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::topLeft);
        if (inUse)
        {
            auto pill = juce::Rectangle<float> (r.getRight() - 86, r.getCentreY() - 12, 70, 24);
            g.setColour (col::ember.withAlpha (0.18f));
            g.fillRoundedRectangle (pill, 12.0f);
            g.setColour (col::text);
            g.setFont (Fonts::semi (12.0f));
            g.drawText ("In use", pill, juce::Justification::centred);
        }
    };
    drawRow (0, "Built-in shaping", "No profile. Harmonies use generic tone and timing.", cur.isEmpty());
    for (int i = 0; i < files.size(); ++i)
    {
        if (rowRect (i + 1).getBottom() > listArea.getBottom()) break;
        auto p = VoiceProfile::load (files[i]);
        drawRow (i + 1, p.name, p.range + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + noteName (p.lowMidi) + "-" + noteName (p.highMidi)
                                + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  vibrato " + juce::String (p.vibratoRateHz, 1) + " Hz",
                 files[i].getFileName() == cur);
    }
}

void VoiceLabPicker::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) { if (onClose) onClose(); return; }
    for (int i = 0; i <= files.size(); ++i)
    {
        if (! rowRect (i).contains (e.getPosition())) continue;
        if (i == 0) { ctx.session.profileFileName.clear(); ctx.session.setProfile (nullptr); }
        else
        {
            auto p = std::make_shared<const VoiceProfile> (VoiceProfile::load (files[i - 1]));
            ctx.session.profileFileName = files[i - 1].getFileName();
            ctx.session.setProfile (p);
        }
        reload();
        return;
    }
}

bool VoiceLabPicker::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

//==============================================================================
VoiceLabSession::VoiceLabSession (Context& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    auto E = [] (juce::String g, juce::String p, juce::String d, std::vector<std::pair<int, double>> guide, double secs,
                 bool pitched = true, double bpm = 0.0, juce::String kind = "sing")
    {
        Exercise e; e.group = g; e.prompt = p; e.detail = d; e.guide = guide; e.seconds = secs; e.pitched = pitched; e.clickBpm = bpm; e.kind = kind;
        return e;
    };
    auto scale = [] (int start)
    {
        std::vector<std::pair<int, double>> s;
        for (int iv : { 0, 2, 4, 5, 7, 5, 4, 2, 0 }) s.push_back ({ start + iv, 0.62 });
        return s;
    };
    ex = {
        E ("Speaking voice", "Count from one to ten", "Just talk, at your normal volume.", {}, 7.0, false, 0, "speak"),
        E ("Speaking voice", "Read this line out loud", "\"Every road I take keeps leading me back home.\"", {}, 6.0, false, 0, "speak"),
        E ("Soft vowels", "Sing a soft \"ooh\"", "Hold it steady on the guide note.", { { 0, 6.0 } }, 6.0),
        E ("Soft vowels", "Sing a soft \"ahh\"", "Relaxed and easy, on the guide note.", { { 4, 6.0 } }, 6.0),
        E ("Soft vowels", "Sing a soft \"eee\"", "Keep it light.", { { 7, 6.0 } }, 6.0),
        E ("Scales", "Sing \"la\" with the guide", "Five notes up and back down.", scale (0), 5.8, true, 0, "scales"),
        E ("Scales", "Same again, a bit higher", "Follow the guide notes.", scale (5), 5.8, true, 0, "scales"),
        E ("Scales", "Now a bit lower", "Follow the guide notes.", scale (-4), 5.8, true, 0, "scales"),
        E ("Connected phrases", "Slide from low to high on \"ahh\"", "One smooth glide, no breaks.", { { 0, 2.5 }, { 12, 2.5 } }, 5.0),
        E ("Connected phrases", "Make up a short phrase on \"na\"", "Anything you like. Keep it flowing.", {}, 7.0),
        E ("Full voice", "Strong, full \"hey\"", "Your chest voice, with some power.", { { 7, 5.0 } }, 5.0),
        E ("Full voice", "Stronger and a little higher", "Push, but don't strain.", { { 9, 5.0 } }, 5.0),
        E ("Head voice", "Light, high \"ooh\"", "Switch to your head voice.", { { 14, 5.0 } }, 5.0),
        E ("Head voice", "Higher still, soft", "Float it. Stop if it hurts.", { { 16, 5.0 } }, 5.0),
        E ("Rhythm", "Sing \"da\" on every click", "Right on the beat.", {}, 6.0, true, 90.0, "rhythm"),
        E ("Rhythm", "Sing \"da\" between the clicks", "On the off-beats this time.", {}, 6.0, true, 90.0, "rhythm"),
        E ("Your song", "Sing part of one of your songs", "About 15 seconds, the way you'd perform it.", {}, 15.0),
        E ("A different mood", "Sing something softly, almost a whisper", "Breathy and close to the mic.", {}, 8.0),
        E ("Check phrases", "One more soft \"ooh\"", "Steady on the guide note.", { { 0, 6.0 } }, 6.0),
        E ("Check phrases", "One more scale on \"la\"", "Follow the guide.", scale (2), 5.8, true, 0, "scales"),
    };

    nameEdit.setText ("My voice", juce::dontSendNotification);
    nameEdit.setFont (Fonts::medium (14.5f));
    nameEdit.setIndents (12, 9);
    rangeCombo.addItemList ({ "Low voice (bass, baritone)", "Middle voice (tenor, alto)", "High voice (soprano, high tenor)" }, 1);
    rangeCombo.setSelectedId (2, juce::dontSendNotification);
    setupCombo (rangeCombo);
    addAndMakeVisible (nameEdit);
    addAndMakeVisible (rangeCombo);

    guideVol.setSliderStyle (juce::Slider::LinearHorizontal);
    guideVol.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    guideVol.setRange (0.0, 1.0);
    guideVol.setValue (ctx.proc.guideLevel.load(), juce::dontSendNotification);
    guideVol.onValueChange = [this] { ctx.proc.guideLevel.store ((float) guideVol.getValue()); ctx.proc.clickLevel.store ((float) guideVol.getValue() * 1.2f); };
    guideVol.setTooltip ("Guide note and click volume");
    addAndMakeVisible (guideVol);

    styleButton (primaryBtn, "primary");
    styleButton (secondaryBtn, "secondary");
    styleButton (skipBtn, "ghost");
    styleButton (closeBtn, "secondary");
    primaryBtn.onClick = [this]
    {
        switch (state)
        {
            case State::Welcome: current = 0; state = State::Ready; break;
            case State::Ready:   startTake(); break;
            case State::Result:  if (resultOk) next(); else startTake(); break;
            case State::Done:    if (onClose) onClose(); return;
            default: break;
        }
        layoutButtons();
        repaint();
    };
    secondaryBtn.onClick = [this]
    {
        if (state == State::Result) { startTake(); }
        else if (state == State::Done) { round++; current = 0; state = State::Ready; }
        else if (state == State::CountIn || state == State::Recording) { ctx.proc.stopFreeCapture(); ctx.proc.guideMidi.store (0); ctx.proc.clickBpm.store (0); state = State::Ready; }
        layoutButtons();
        repaint();
    };
    skipBtn.onClick = [this] { if (state == State::Ready || state == State::Result) next(); layoutButtons(); repaint(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &primaryBtn, &secondaryBtn, &skipBtn, &closeBtn }) addAndMakeVisible (b);
    layoutButtons();
    startTimerHz (30);
}

VoiceLabSession::~VoiceLabSession()
{
    alive->store (false);
    ctx.proc.guideMidi.store (0.0f);
    ctx.proc.clickBpm.store (0.0f);
    if (ctx.proc.captureState() == HarmivoxProcessor::Capture::Free) ctx.proc.stopFreeCapture();
}

int VoiceLabSession::baseMidi() const
{
    switch (rangeCombo.getSelectedId()) { case 1: return 43; case 3: return 57; default: return 50; }
}

std::vector<juce::String> VoiceLabSession::groups() const
{
    std::vector<juce::String> g;
    for (auto& e : ex) if (g.empty() || g.back() != e.group) g.push_back (e.group);
    return g;
}

void VoiceLabSession::layoutButtons()
{
    nameEdit.setVisible (state == State::Welcome);
    rangeCombo.setVisible (state == State::Welcome);
    skipBtn.setVisible (state == State::Ready || state == State::Result);
    secondaryBtn.setVisible (state == State::Result || state == State::Done || state == State::CountIn || state == State::Recording);
    primaryBtn.setVisible (state != State::CountIn && state != State::Recording && state != State::Building);
    primaryBtn.setEnabled (true);
    switch (state)
    {
        case State::Welcome:  primaryBtn.setButtonText ("Start"); break;
        case State::Ready:    primaryBtn.setButtonText ("Record take"); break;
        case State::Result:   primaryBtn.setButtonText (resultOk ? (current + 1 < (int) ex.size() ? "Next" : "Build my voice") : "Try again");
                              secondaryBtn.setButtonText ("Redo"); secondaryBtn.setVisible (resultOk); break;
        case State::Done:     primaryBtn.setButtonText ("Done"); secondaryBtn.setButtonText ("Record extra rounds"); break;
        case State::CountIn:
        case State::Recording: secondaryBtn.setButtonText ("Stop"); break;
        default: break;
    }
    resized();
}

void VoiceLabSession::startTake()
{
    const auto& e = ex[(size_t) current];
    state = State::CountIn;
    phaseStart = ctx.proc.labClockSec();
    ctx.proc.clickStartSec.store (phaseStart);
    ctx.proc.clickBpm.store (90.0f);
    ctx.proc.guideMidi.store (e.guide.empty() ? 0.0f : (float) (baseMidi() + e.guide.front().first));
    layoutButtons();
}

void VoiceLabSession::next()
{
    if (current + 1 < (int) ex.size()) { current++; state = State::Ready; }
    else build();
}

void VoiceLabSession::finishTake()
{
    ctx.proc.guideMidi.store (0.0f);
    ctx.proc.clickBpm.store (0.0f);
    auto audio = ctx.proc.stopFreeCapture();
    const double sr = ctx.proc.currentSampleRate();
    const auto& e = ex[(size_t) current];

    resultOk = false;
    if (audio.getNumSamples() < (int) (sr * 1.0)) { resultText = "Nothing came in. Check that your mic is routed to this mixer insert."; state = State::Result; layoutButtons(); return; }

    const float peakDb = juce::Decibels::gainToDecibels (audio.getMagnitude (0, 0, audio.getNumSamples()), -120.0f);
    auto src = Analysis::analyse (audio, sr, "lab");
    int voiced = 0;
    std::vector<float> ps;
    for (auto& f : src->frames) if (f.voiced) { ++voiced; ps.push_back (f.midi); }
    const float voicedFrac = src->frames.empty() ? 0.0f : (float) voiced / (float) src->frames.size();

    if (peakDb < -38.0f) resultText = "Too quiet. Move closer to the mic or turn up the input.";
    else if (peakDb > -0.3f) resultText = "Too loud: it clipped. Back off a little or lower the input.";
    else if (voicedFrac < (e.pitched ? 0.22f : 0.10f)) resultText = "I couldn't hear much voice. Try again, a bit louder.";
    else if (e.pitched && ! e.guide.empty() && e.kind != "scales" && e.guide.size() == 1)
    {
        const float target = (float) (baseMidi() + e.guide.front().first);
        float med = Analysis::medianOf (ps);
        float diff = std::fmod (std::abs (med - target), 12.0f);
        diff = juce::jmin (diff, 12.0f - diff);
        if (diff > 2.0f) resultText = "Pitch was away from the guide (" + noteName (med) + " vs " + noteName (target) + "). Try matching it.";
        else resultOk = true;
    }
    else resultOk = true;

    if (resultOk)
    {
        takes[current].push_back (src);
        resultText = juce::String ("Good take.") + (voicedFrac > 0.5f ? " Nice and steady." : "");
    }
    state = State::Result;
    layoutButtons();
}

void VoiceLabSession::build()
{
    state = State::Building;
    building.store (true);
    layoutButtons();
    std::vector<VoiceProfile::Take> all;
    for (auto& [i, v] : takes)
        for (auto& s : v) all.push_back ({ s, ex[(size_t) i].kind, ex[(size_t) i].clickBpm });
    const auto name = nameEdit.getText().trim().isEmpty() ? juce::String ("My voice") : nameEdit.getText().trim();
    const auto range = rangeCombo.getText();
    auto a = alive;
    auto* self = this;
    juce::Thread::launch ([all, name, range, a, self]
    {
        auto p = VoiceProfile::build (all, name, range);
        auto file = VoiceProfile::folder().getChildFile (juce::File::createLegalFileName (name) + ".hvxvoice");
        p.save (file);
        juce::MessageManager::callAsync ([a, self, p, file]
        {
            if (! a->load()) return;
            self->ctx.session.profileFileName = file.getFileName();
            self->ctx.session.setProfile (std::make_shared<const VoiceProfile> (p));
            self->builtName = p.name;
            self->building.store (false);
            self->state = p.valid ? State::Done : State::Result;
            if (! p.valid) { self->resultOk = false; self->resultText = "Not enough clean singing to learn from. Record a few more phrases."; }
            self->layoutButtons();
            self->repaint();
        });
    });
}

void VoiceLabSession::timerCallback()
{
    // live pitch
    float buf[4096];
    const int n = ctx.proc.copyRecentInput (buf, 4096);
    float m = 0, cl = 0;
    if (ctx.proc.inputLevel() > 0.01f && Analysis::quickPitch (buf, n, ctx.proc.currentSampleRate(), m, cl)) livePitch = m;
    else livePitch *= 0.0f;

    const double now = ctx.proc.labClockSec();
    if (state == State::CountIn && now - phaseStart >= 4 * 60.0 / 90.0)
    {
        state = State::Recording;
        phaseStart = now;
        ctx.proc.startFreeCapture();
        if (ex[(size_t) current].clickBpm <= 0) ctx.proc.clickBpm.store (0.0f);
        else ctx.proc.clickStartSec.store (now);
        layoutButtons();
    }
    if (state == State::Recording)
    {
        const auto& e = ex[(size_t) current];
        const double t = now - phaseStart;
        double acc = 0; float g = 0;
        for (auto& [off, len] : e.guide) { if (t >= acc && t < acc + len) { g = (float) (baseMidi() + off); break; } acc += len; }
        ctx.proc.guideMidi.store (g);
        if (t >= e.seconds) finishTake();
    }
    repaint();
}

void VoiceLabSession::resized()
{
    auto r = getLocalBounds().reduced (22);
    auto top = r.removeFromTop (56);
    closeBtn.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 34));
    top.removeFromRight (20);
    guideVol.setBounds (top.removeFromRight (180).withSizeKeepingCentre (180, 24));
    r.removeFromTop (12);
    r.removeFromLeft (270);
    auto main = r.reduced (34, 30);
    auto foot = main.removeFromBottom (46);
    primaryBtn.setBounds (foot.removeFromRight (190));
    foot.removeFromRight (10);
    secondaryBtn.setBounds (foot.removeFromRight (190));
    skipBtn.setBounds (foot.removeFromLeft (90));
    if (state == State::Welcome)
    {
        auto row = main.withTrimmedTop (230).removeFromTop (38);
        nameEdit.setBounds (row.removeFromLeft (row.getWidth() / 2 - 6));
        row.removeFromLeft (12);
        rangeCombo.setBounds (row);
    }
}

void VoiceLabSession::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds().reduced (22);
    auto top = r.removeFromTop (56);
    auto logo = top.removeFromLeft (46).toFloat().withSizeKeepingCentre (42, 42);
    g.setGradientFill (accentGradient (logo));
    g.fillRoundedRectangle (logo, 10.0f);
    Icons::draw (g, Icons::Id::mic, logo.reduced (10), juce::Colour (0xff1a0b0b), 2.0f);
    top.removeFromLeft (12);
    g.setColour (col::text);
    g.setFont (Fonts::display (24.0f));
    g.drawText ("Voice lab", top.removeFromTop (28), juce::Justification::bottomLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (13.0f));
    g.drawText ("Teach HARMIVOX-6 your voice. Guided, about 10 minutes.", top, juce::Justification::topLeft);
    g.setColour (col::textFaint);
    g.setFont (Fonts::medium (12.5f));
    g.drawText ("Guide", guideVol.getX() - 50, guideVol.getY(), 44, guideVol.getHeight(), juce::Justification::centredRight);
    r.removeFromTop (12);

    // sidebar
    auto side = r.removeFromLeft (256).toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (side, 12.0f);
    auto sr = side.reduced (16, 16).toNearestInt();
    drawSectionLabel (g, "Your session", sr.removeFromTop (22));
    sr.removeFromTop (6);
    const auto gs = groups();
    const juce::String curGroup = state == State::Welcome ? "" : ex[(size_t) current].group;
    {
        auto row = sr.removeFromTop (34);
        const bool on = state == State::Welcome;
        if (on) { g.setColour (col::raisedHi); g.fillRoundedRectangle (row.toFloat(), 8.0f); }
        g.setColour (on ? col::ember : col::good);
        g.drawEllipse (juce::Rectangle<float> (12, 12).withCentre ({ (float) row.getX() + 16, (float) row.getCentreY() }), 1.6f);
        if (! on) g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ (float) row.getX() + 16, (float) row.getCentreY() }));
        g.setColour (on ? col::text : col::textDim);
        g.setFont (Fonts::semi (13.5f));
        g.drawText ("Welcome and microphone", row.withTrimmedLeft (32), juce::Justification::centredLeft);
    }
    for (auto& grp : gs)
    {
        int total = 0, done = 0;
        for (int i = 0; i < (int) ex.size(); ++i)
            if (ex[(size_t) i].group == grp) { ++total; if (! takes[i].empty()) ++done; }
        auto row = sr.removeFromTop (34);
        const bool on = grp == curGroup;
        if (on) { g.setColour (col::raisedHi); g.fillRoundedRectangle (row.toFloat(), 8.0f); }
        const auto dotC = done == total ? col::good : (on ? col::ember : col::borderHi);
        g.setColour (dotC);
        g.drawEllipse (juce::Rectangle<float> (12, 12).withCentre ({ (float) row.getX() + 16, (float) row.getCentreY() }), 1.6f);
        if (done == total) g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ (float) row.getX() + 16, (float) row.getCentreY() }));
        g.setColour (on ? col::text : col::textDim);
        g.setFont (on ? Fonts::semi (13.5f) : Fonts::medium (13.5f));
        g.drawText (grp, row.withTrimmedLeft (32), juce::Justification::centredLeft);
        if (total > 1)
        {
            g.setColour (col::textFaint);
            g.setFont (Fonts::mono (12.0f));
            g.drawText (juce::String (done) + "/" + juce::String (total), row.withTrimmedRight (8), juce::Justification::centredRight);
        }
    }
    g.setColour (col::textFaint);
    g.setFont (Fonts::body (12.0f));
    int total = 0; for (auto& [i, v] : takes) total += (int) v.size();
    g.drawText (juce::String (total) + " takes recorded" + (round > 1 ? "  (round " + juce::String (round) + ")" : juce::String()),
                side.reduced (16, 14).removeFromBottom (18).toNearestInt(), juce::Justification::centredLeft);

    r.removeFromLeft (14);
    auto mainF = r.toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (mainF, 12.0f);
    auto m = r.reduced (34, 30);

    auto meter = [&] (juce::Rectangle<int> area)
    {
        drawSectionLabel (g, "Microphone", area.removeFromTop (20));
        area.removeFromTop (6);
        auto bar = area.removeFromTop (8).toFloat();
        g.setColour (col::raised);
        g.fillRoundedRectangle (bar, 4.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, std::pow (ctx.proc.inputLevel(), 0.5f));
        g.setGradientFill (accentGradient (bar));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * lvl), 4.0f);
        for (float mark : { 0.56f, 0.89f })
        {
            g.setColour (col::textFaint);
            g.fillRect (bar.getX() + bar.getWidth() * mark, bar.getY() - 3, 1.5f, bar.getHeight() + 6);
        }
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.5f));
        g.drawText ("Sing loudly: aim for the meter to peak between the two marks.", area.removeFromTop (24), juce::Justification::centredLeft);
    };

    if (state == State::Welcome)
    {
        drawSectionLabel (g, "Getting started", m.removeFromTop (20), col::ember);
        g.setColour (col::text);
        g.setFont (Fonts::display (30.0f));
        g.drawText ("Create your voice", m.removeFromTop (40), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (Fonts::body (14.0f));
        g.drawText ("HARMIVOX-6 learns how you sing, then uses it for new doubles and harmonies. Everything stays on this computer.",
                    m.removeFromTop (24), juce::Justification::centredLeft);
        m.removeFromTop (14);
        for (auto s : { "Put on headphones, so the guide note stays out of your takes.",
                        "Route your mic into this mixer insert in FL (or use the standalone app).",
                        "Sing one short phrase at a time. Each take is checked.",
                        "At the end your voice is built and selected automatically." })
        {
            auto row = m.removeFromTop (28);
            auto tick = row.removeFromLeft (26).toFloat().withSizeKeepingCentre (18, 18);
            g.setColour (col::ember.withAlpha (0.18f));
            g.fillEllipse (tick);
            Icons::draw (g, Icons::Id::check, tick.reduced (3), col::ember, 2.0f);
            g.setColour (col::text);
            g.setFont (Fonts::body (14.0f));
            g.drawText (s, row, juce::Justification::centredLeft);
        }
        m.removeFromTop (60);
        meter (m.removeFromTop (70));
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.5f));
        g.drawText ("20 short phrases  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  about 10 minutes  "
                    + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  building runs by itself",
                    primaryBtn.getX() - 460, primaryBtn.getY(), 450, primaryBtn.getHeight(), juce::Justification::centredRight);
        return;
    }

    if (state == State::Building || state == State::Done)
    {
        auto c = m.withSizeKeepingCentre (m.getWidth(), 200);
        g.setColour (col::text);
        g.setFont (Fonts::display (30.0f));
        g.drawText (state == State::Building ? "Building your voice..." : "\"" + builtName + "\" is ready", c.removeFromTop (44), juce::Justification::centred);
        g.setColour (col::textDim);
        g.setFont (Fonts::body (14.5f));
        if (state == State::Building)
        {
            g.drawText ("Learning your tone across your range, your vibrato and your timing.", c.removeFromTop (26), juce::Justification::centred);
            const float ph = (float) std::fmod (juce::Time::getMillisecondCounter() / 900.0, 1.0);
            auto bar = c.removeFromTop (40).toFloat().withSizeKeepingCentre (300, 4);
            g.setColour (col::raised); g.fillRoundedRectangle (bar, 2);
            auto seg = bar.withWidth (90).withX (bar.getX() + (bar.getWidth() - 90) * ph);
            g.setGradientFill (accentGradient (seg)); g.fillRoundedRectangle (seg, 2);
        }
        else
        {
            g.drawText ("It's selected now. New voices you add will keep your tone.", c.removeFromTop (26), juce::Justification::centred);
            if (auto p = ctx.session.profile())
                g.drawText ("Range " + noteName (p->lowMidi) + " to " + noteName (p->highMidi) + "   " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                            + "   vibrato " + juce::String (p->vibratoRateHz, 1) + " Hz, " + juce::String (juce::roundToInt (p->vibratoDepthCents)) + " cents"
                            + "   " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "   timing feel " + juce::String (juce::roundToInt (p->looseMs)) + " ms",
                            c.removeFromTop (26), juce::Justification::centred);
            g.setColour (col::textFaint);
            g.drawText ("More singing gives better results: record extra rounds of the same phrases any time.", c.removeFromTop (26), juce::Justification::centred);
        }
        return;
    }

    const auto& e = ex[(size_t) current];
    drawSectionLabel (g, e.group + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  phrase " + juce::String (current + 1) + " of " + juce::String ((int) ex.size()),
                      m.removeFromTop (20), col::ember);
    g.setColour (col::text);
    g.setFont (Fonts::display (34.0f));
    g.drawText (e.prompt, m.removeFromTop (46), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (Fonts::body (15.0f));
    g.drawText (e.detail, m.removeFromTop (26), juce::Justification::centredLeft);
    m.removeFromTop (24);

    // status / countdown
    auto status = m.removeFromTop (54);
    const double now = ctx.proc.labClockSec();
    if (state == State::CountIn)
    {
        const int beat = 4 - (int) std::floor ((now - phaseStart) / (60.0 / 90.0));
        g.setColour (col::amber);
        g.setFont (Fonts::display (40.0f));
        g.drawText (juce::String (juce::jmax (1, beat)), status.removeFromLeft (60), juce::Justification::centredLeft);
        g.setFont (Fonts::semi (15.0f));
        g.drawText (e.guide.empty() ? "Get ready..." : "Listen to the guide note...", status, juce::Justification::centredLeft);
    }
    else if (state == State::Recording)
    {
        const double t = now - phaseStart;
        g.setColour (col::bad);
        g.fillEllipse ((float) status.getX(), status.getCentreY() - 8.0f, 16, 16);
        g.setColour (col::text);
        g.setFont (Fonts::semi (16.0f));
        g.drawText ("Recording", status.withTrimmedLeft (26), juce::Justification::centredLeft);
        auto bar = status.withTrimmedLeft (140).toFloat().withSizeKeepingCentre ((float) status.getWidth() - 160, 6);
        g.setColour (col::raised); g.fillRoundedRectangle (bar, 3);
        g.setGradientFill (accentGradient (bar));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, t / e.seconds)), 3);
    }
    else if (state == State::Result)
    {
        auto ic = status.removeFromLeft (40).toFloat().withSizeKeepingCentre (30, 30);
        g.setColour ((resultOk ? col::good : col::bad).withAlpha (0.18f));
        g.fillEllipse (ic);
        Icons::draw (g, resultOk ? Icons::Id::check : Icons::Id::cross, ic.reduced (7), resultOk ? col::good : col::bad, 2.2f);
        g.setColour (col::text);
        g.setFont (Fonts::semi (15.0f));
        g.drawText (resultText, status.withTrimmedLeft (10), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (col::textDim);
        g.setFont (Fonts::medium (14.0f));
        g.drawText ("Press Record take. You'll hear 4 clicks" + juce::String (e.guide.empty() ? "" : " and the guide note") + ", then sing.",
                    status, juce::Justification::centredLeft);
    }
    m.removeFromTop (20);

    // pitch strip: guide vs you
    auto strip = m.removeFromTop (110).toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (strip, 10.0f);
    const float lo = (float) baseMidi() - 7.0f, hi = (float) baseMidi() + 19.0f;
    auto yFor = [&] (float midi) { return strip.getBottom() - 10 - (midi - lo) / (hi - lo) * (strip.getHeight() - 20); };
    for (int mm = (int) lo; mm <= (int) hi; ++mm)
        if (mm % 12 == 0)
        {
            g.setColour (col::border);
            g.drawHorizontalLine ((int) yFor ((float) mm), strip.getX() + 40, strip.getRight() - 10);
            g.setColour (col::textFaint);
            g.setFont (Fonts::semi (11.0f));
            g.drawText (noteName ((float) mm), juce::Rectangle<float> (strip.getX() + 6, yFor ((float) mm) - 8, 32, 16), juce::Justification::centredLeft);
        }
    const float gm = ctx.proc.guideMidi.load();
    if (gm > 0)
    {
        g.setColour (col::amber.withAlpha (0.8f));
        g.fillRoundedRectangle (juce::Rectangle<float> (strip.getX() + 46, yFor (gm) - 4, strip.getWidth() - 60, 8), 4.0f);
        g.setColour (col::amber);
        g.setFont (Fonts::semi (12.0f));
        g.drawText ("guide " + noteName (gm), juce::Rectangle<float> (strip.getRight() - 110, yFor (gm) - 22, 100, 16), juce::Justification::centredRight);
    }
    if (livePitch > 0)
    {
        float lp = livePitch;
        if (gm > 0) { while (lp - gm > 6) lp -= 12; while (gm - lp > 6) lp += 12; }
        const float y = juce::jlimit (strip.getY() + 6, strip.getBottom() - 6, yFor (lp));
        g.setGradientFill (accentGradient (strip));
        g.fillEllipse (strip.getCentreX() - 9, y - 9, 18, 18);
        g.setColour (col::text);
        g.setFont (Fonts::semi (12.0f));
        g.drawText ("you " + noteName (livePitch), juce::Rectangle<float> (strip.getCentreX() + 16, y - 8, 90, 16), juce::Justification::centredLeft);
    }
    m.removeFromTop (16);
    meter (m.removeFromTop (70));
}

void VoiceLabSession::mouseDown (const juce::MouseEvent&) {}

bool VoiceLabSession::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == juce::KeyPress::spaceKey && primaryBtn.isVisible()) { primaryBtn.triggerClick(); return true; }
    return false;
}
} // namespace hvx::ui
