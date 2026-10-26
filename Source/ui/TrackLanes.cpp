#include "TrackLanes.h"
#include "../engine/Aligner.h"

namespace hvx::ui
{
//==============================================================================
void drawRuler (juce::Graphics& g, Context& ctx, juce::Rectangle<float> r, float left)
{
    const double bar = ctx.barSeconds();
    const double beat = bar / ctx.session.doc.beatsPerBar;
    const double pxBar = bar * ctx.ui.pxPerSec;
    int labelEvery = 1;
    while (pxBar * labelEvery < 46.0) labelEvery *= 2;

    const double t0 = ctx.xToSec (r.getX(), left), t1 = ctx.xToSec (r.getRight(), left);
    g.setFont (Fonts::mono (12.5f));
    for (int b = juce::jmax (0, (int) std::floor (t0 / bar)); b <= (int) std::ceil (t1 / bar); ++b)
    {
        const float x = (float) ctx.secToX (b * bar, left);
        if (b % labelEvery == 0)
        {
            g.setColour (col::textFaint);
            g.drawVerticalLine ((int) x, r.getY() + r.getHeight() * 0.45f, r.getBottom());
            g.setColour (col::textDim);
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 4.0f, r.getY() + 2.0f, 40.0f, r.getHeight() * 0.6f), juce::Justification::centredLeft);
        }
        if (pxBar > 60.0)
            for (int k = 1; k < ctx.session.doc.beatsPerBar; ++k)
            {
                const float bx = (float) ctx.secToX (b * bar + k * beat, left);
                g.setColour (col::border);
                g.drawVerticalLine ((int) bx, r.getBottom() - 6.0f, r.getBottom());
            }
    }
}

void drawGrid (juce::Graphics& g, Context& ctx, juce::Rectangle<float> r, float left)
{
    const double bar = ctx.barSeconds();
    const double beat = bar / ctx.session.doc.beatsPerBar;
    const double t0 = ctx.xToSec (r.getX(), left), t1 = ctx.xToSec (r.getRight(), left);
    const bool showBeats = beat * ctx.ui.pxPerSec > 14.0;
    for (int b = juce::jmax (0, (int) std::floor (t0 / bar)); b <= (int) std::ceil (t1 / bar); ++b)
    {
        g.setColour (col::border.withAlpha (0.9f));
        g.drawVerticalLine ((int) ctx.secToX (b * bar, left), r.getY(), r.getBottom());
        if (showBeats)
            for (int k = 1; k < ctx.session.doc.beatsPerBar; ++k)
            {
                g.setColour (col::grid);
                g.drawVerticalLine ((int) ctx.secToX (b * bar + k * beat, left), r.getY(), r.getBottom());
            }
    }
}

void drawPlayhead (juce::Graphics& g, Context& ctx, juce::Rectangle<float> r, float left)
{
    const float x = (float) ctx.secToX (ctx.proc.positionSeconds(), left);
    if (x < r.getX() - 1 || x > r.getRight() + 1) return;
    g.setColour (col::ember);
    g.fillRect (juce::Rectangle<float> (x - 0.75f, r.getY(), 1.5f, r.getHeight()));
}

//==============================================================================
class TrackLanes::Ruler : public juce::Component
{
public:
    explicit Ruler (Context& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        auto r = getLocalBounds().toFloat();
        g.setColour (col::textFaint);
        drawSectionLabel (g, "Tracks", getLocalBounds().withWidth (headerW).withTrimmedLeft (14));
        auto area = r.withTrimmedLeft ((float) headerW);
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        drawRuler (g, ctx, area, (float) headerW);
        // playhead cap
        const float x = (float) ctx.secToX (ctx.proc.positionSeconds(), (float) headerW);
        if (x >= area.getX())
        {
            juce::Path p;
            p.addTriangle (x - 6, r.getY() + 2, x + 6, r.getY() + 2, x, r.getY() + 11);
            g.setColour (col::ember);
            g.fillPath (p);
            g.fillRect (x - 0.75f, r.getY() + 8, 1.5f, r.getHeight());
        }
        g.restoreState();
        g.setColour (col::border);
        g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
    void mouseDown (const juce::MouseEvent& e) override { seek (e); }
    void mouseDrag (const juce::MouseEvent& e) override { seek (e); }
    void seek (const juce::MouseEvent& e)
    {
        if (e.x < headerW) return;
        ctx.proc.setInternalPosition (juce::jmax (0.0, ctx.xToSec (e.x, (float) headerW)));
        getParentComponent()->repaint();
    }
    Context& ctx;
};

//==============================================================================
class TrackLanes::ChordLane : public juce::Component
{
public:
    explicit ChordLane (Context& c) : ctx (c)
    {
        styleButton (autoBtn, "chip");
        autoBtn.setClickingTogglesState (true);
        autoBtn.setTooltip ("Auto: chords follow the lead melody. Click a chord to set it yourself.");
        autoBtn.onClick = [this]
        {
            ctx.session.checkpoint ("Chord lane auto");
            ctx.session.doc.autoChords = autoBtn.getToggleState();
            ctx.session.changed();
        };
        addAndMakeVisible (autoBtn);
    }
    void resized() override { autoBtn.setBounds (headerW - 74, 7, 58, 20); }
    void paint (juce::Graphics& g) override
    {
        autoBtn.setToggleState (ctx.session.doc.autoChords, juce::dontSendNotification);
        g.fillAll (col::panel);
        drawSectionLabel (g, "Chords", getLocalBounds().withWidth (100).withTrimmedLeft (14));
        auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) headerW);
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        for (auto& c : ctx.session.doc.chords)
        {
            const float x0 = (float) ctx.secToX (c.start, (float) headerW), x1 = (float) ctx.secToX (c.end, (float) headerW);
            auto box = juce::Rectangle<float> (x0 + 2, 5, x1 - x0 - 4, (float) getHeight() - 10);
            if (box.getRight() < area.getX() || box.getX() > area.getRight()) continue;
            g.setColour (col::raised);
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (c.root >= 0 ? col::borderHi : col::border);
            g.drawRoundedRectangle (box, 5.0f, 1.0f);
            g.setColour (col::text);
            g.setFont (Fonts::semi (13.5f));
            g.drawText (chordName (c), box.reduced (8, 0), juce::Justification::centredLeft, true);
        }
        g.restoreState();
        g.setColour (col::border);
        g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.x < headerW) return;
        const double t = ctx.xToSec (e.x, (float) headerW);
        int idx = -1;
        auto& chords = ctx.session.doc.chords;
        for (int i = 0; i < (int) chords.size(); ++i) if (t >= chords[(size_t) i].start && t < chords[(size_t) i].end) idx = i;
        const double bar = ctx.barSeconds();

        juce::PopupMenu m;
        m.addSectionHeader ("Chord for bar " + juce::String ((int) std::floor (t / bar) + 1));
        for (int root = 0; root < 12; ++root)
        {
            juce::PopupMenu sub;
            for (int q = 0; q < chordQualityNames().size(); ++q)
                sub.addItem (1000 + root * 20 + q, keyNames()[root] + chordQualityNames()[q]);
            m.addSubMenu (keyNames()[root], sub);
        }
        m.addItem (999, "No chord (N.C.)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                         [this, t, bar, idx] (int r)
        {
            if (r == 0) return;
            ctx.session.checkpoint ("Set chord");
            auto& d = ctx.session.doc;
            d.autoChords = false;
            const double bs = std::floor (t / bar) * bar;
            Chord c { bs, bs + bar, r == 999 ? -1 : (r - 1000) / 20, r == 999 ? 0 : (r - 1000) % 20 };
            // split the existing chord around this bar
            std::vector<Chord> out;
            for (auto& o : d.chords)
            {
                if (o.end <= c.start || o.start >= c.end) { out.push_back (o); continue; }
                if (o.start < c.start) out.push_back ({ o.start, c.start, o.root, o.quality });
                if (o.end > c.end) out.push_back ({ c.end, o.end, o.root, o.quality });
            }
            out.push_back (c);
            std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.start < b.start; });
            d.chords = out;
            juce::ignoreUnused (idx);
            ctx.session.changed();
        });
    }
    Context& ctx;
    juce::TextButton autoBtn { "AUTO" };
};

//==============================================================================
class TrackLanes::LaneHeader : public juce::Component
{
public:
    LaneHeader (Context& c, int id) : ctx (c), trackId (id)
    {
        for (auto* b : { &mute, &solo, &edit, &more }) { b->framed = true; addAndMakeVisible (b); }
        mute.setClickingTogglesState (true);
        solo.setClickingTogglesState (true);
        solo.onColour = col::amber;
        mute.onClick = [this] { if (auto* t = track()) { t->mute = mute.getToggleState(); ctx.session.changed(); } };
        solo.onClick = [this] { if (auto* t = track()) { t->solo = solo.getToggleState(); ctx.session.changed(); } };
        edit.onClick = [this] { ctx.select (trackId); ctx.ui.notesView = true; ctx.ui.notify(); };
        more.onClick = [this] { showMenu(); };

        for (auto* s : { &vol, &pan })
        {
            s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
            s->setMouseDragSensitivity (160);
            addAndMakeVisible (s);
        }
        vol.setRange (-30.0, 6.0); vol.setDoubleClickReturnValue (true, 0.0); vol.setSkewFactorFromMidPoint (-6.0);
        pan.setRange (-1.0, 1.0); pan.setDoubleClickReturnValue (true, 0.0); pan.getProperties().set ("bipolar", true);
        vol.onDragStart = [this] { ctx.session.checkpoint ("Volume"); };
        pan.onDragStart = [this] { ctx.session.checkpoint ("Pan"); };
        vol.onValueChange = [this]
        {
            if (auto* t = track()) { t->gainDb = (float) vol.getValue(); vol.setTooltip ("Volume " + formatDb (t->gainDb)); ctx.session.changed(); }
        };
        pan.onValueChange = [this]
        {
            if (auto* t = track())
            {
                t->pan = (float) pan.getValue();
                const int p = juce::roundToInt (t->pan * 100);
                pan.setTooltip ("Pan " + (p == 0 ? juce::String ("centre") : (p < 0 ? "L " : "R ") + juce::String (std::abs (p))));
                ctx.session.changed();
            }
        };

        name.setFont (Fonts::semi (14.5f));
        name.setColour (juce::Label::textColourId, col::text);
        name.setEditable (false, true, false);
        name.setInterceptsMouseClicks (false, false);
        name.onTextChange = [this]
        {
            if (auto* t = track()) { ctx.session.checkpoint ("Rename"); t->name = name.getText(); ctx.session.changed(); }
        };
        addAndMakeVisible (name);
        refresh();
    }

    Track* track() { return ctx.session.doc.find (trackId); }

    void refresh()
    {
        auto* t = track();
        if (t == nullptr) return;
        if (! name.isBeingEdited()) name.setText (t->name, juce::dontSendNotification);
        mute.setToggleState (t->mute, juce::dontSendNotification);
        solo.setToggleState (t->solo, juce::dontSendNotification);
        vol.setValue (t->gainDb, juce::dontSendNotification);
        pan.setValue (t->pan, juce::dontSendNotification);
        mute.setIcon (t->mute ? Icons::Id::speakerOff : Icons::Id::speaker);
        mute.setTooltip (t->mute ? "Unmute" : "Mute");
        solo.setTooltip ("Solo");
        edit.setTooltip ("Edit notes");
        more.setTooltip ("More");
        vol.setTooltip ("Volume " + formatDb (t->gainDb));
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 0).withTrimmedLeft (2);
        name.setBounds (r.getX() - 4, 8, r.getWidth() - 54, 22);
        auto row = juce::Rectangle<int> (r.getX(), 50, r.getWidth(), 28);
        more.setBounds (row.removeFromRight (28));
        for (auto* b : { &mute, &solo, &edit }) { b->setBounds (row.removeFromLeft (28)); row.removeFromLeft (5); }
        row.removeFromLeft (6);
        vol.setBounds (row.removeFromLeft (30)); row.removeFromLeft (4);
        pan.setBounds (row.removeFromLeft (30));
    }

    void paint (juce::Graphics& g) override
    {
        auto* t = track();
        if (t == nullptr) return;
        const bool sel = ctx.ui.selectedTrack == trackId;
        g.setColour (sel ? col::raised.withAlpha (0.55f) : col::panel);
        g.fillRect (getLocalBounds());
        g.setColour (t->colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 8.0f, 3.0f, (float) getHeight() - 16.0f), 1.5f);

        // badge
        auto badge = juce::Rectangle<float> ((float) getWidth() - 58.0f, 9.0f, 44.0f, 19.0f);
        if (t->isLead)
        {
            g.setGradientFill (accentGradient (badge, 0.9f));
            g.fillRoundedRectangle (badge, 5.0f);
            g.setColour (juce::Colour (0xff1a0b0b));
        }
        else
        {
            g.setColour (col::raised);
            g.fillRoundedRectangle (badge, 5.0f);
            g.setColour (col::border);
            g.drawRoundedRectangle (badge, 5.0f, 1.0f);
            g.setColour (col::textFaint);
        }
        g.setFont (Fonts::bold (11.0f).withExtraKerningFactor (0.08f));
        juce::String btxt = "LEAD";
        if (t->isVoice())
        {
            const int s = t->voice.steps;
            btxt = s == 0 ? "DBL" : Harmony::intervalLabels()[juce::jlimit (0, 9, std::abs (s))].toUpperCase() + (s > 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb4")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")));
        }
        g.drawText (btxt, badge, juce::Justification::centred);

        // sub line
        const auto st = ctx.session.statusOf (trackId);
        const bool busy = ctx.session.isBusy (trackId);
        juce::String sub;
        juce::Colour sc = col::textDim;
        if (busy) { sub = "Rendering"; sc = col::amber; }
        else if (t->isVoice())
        {
            sub = t->voice.followParent ? "Ready" : "Edited (unlinked)";
            if (t->useProfile && ctx.session.profile() != nullptr && ctx.session.profile()->valid) sub << "  " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << "  my voice";
            sc = t->voice.followParent ? col::good.withAlpha (0.85f) : col::amber;
        }
        else
        {
            const int n = (int) t->notes.size();
            sub = juce::String (n) + " notes  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  "
                  + (t->source ? juce::String (t->source->lengthSeconds(), 1) + "s" : juce::String());
        }
        g.setColour (sc);
        g.setFont (Fonts::medium (12.5f));
        g.drawText (sub, 16, 30, getWidth() - 30, 16, juce::Justification::centredLeft);

        // progress strip
        auto strip = juce::Rectangle<float> (16.0f, (float) getHeight() - 5.0f, (float) getWidth() - 32.0f, 2.0f);
        g.setColour (col::border);
        g.fillRoundedRectangle (strip, 1.0f);
        if (busy)
        {
            const float ph = (float) std::fmod (juce::Time::getMillisecondCounter() / 900.0, 1.0);
            auto seg = strip.withWidth (strip.getWidth() * 0.3f).withX (strip.getX() + (strip.getWidth() * 0.7f) * ph);
            g.setGradientFill (accentGradient (seg));
            g.fillRoundedRectangle (seg, 1.0f);
        }
        else if (st == "Ready")
        {
            g.setColour (t->colour.withAlpha (0.55f));
            g.fillRoundedRectangle (strip, 1.0f);
        }

        g.setColour (col::border);
        g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = track();
        if (t == nullptr) return;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (e.x > getWidth() - 60 && e.y < 30 && ! t->isVoice() && ! t->isLead) { ctx.session.setLead (trackId); return; }
        ctx.select (trackId);
        if (e.getNumberOfClicks() == 2 && e.y < 30) name.showEditor();
    }

    void showMenu()
    {
        auto* t = track();
        if (t == nullptr) return;
        juce::PopupMenu m;
        m.addItem (1, "Rename");
        if (! t->isVoice()) m.addItem (2, "Set as lead", ! t->isLead);
        if (t->isVoice())
        {
            m.addItem (3, "Reroll take (new performance)");
            m.addItem (4, "Re-sync to lead", ! t->voice.followParent);
            m.addItem (5, "Shape with my voice profile", true, t->useProfile);
        }
        m.addItem (6, "Duplicate");
        m.addItem (7, "Edit notes");
        m.addSeparator();
        m.addItem (8, "Delete track");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more), [this] (int r)
        {
            auto* tr = track();
            if (tr == nullptr) return;
            switch (r)
            {
                case 1: name.showEditor(); break;
                case 2: ctx.session.setLead (trackId); break;
                case 3: ctx.session.reroll (trackId); break;
                case 4: ctx.session.resyncVoice (trackId); break;
                case 5: ctx.session.checkpoint ("Voice profile"); tr->useProfile = ! tr->useProfile; ctx.session.changed(); break;
                case 6: { const int id = ctx.session.duplicateTrack (trackId); ctx.select (id); break; }
                case 7: ctx.select (trackId); ctx.ui.notesView = true; ctx.ui.notify(); break;
                case 8: ctx.session.deleteTrack (trackId); break;
                default: break;
            }
        });
    }

    Context& ctx;
    const int trackId;
    IconButton mute { Icons::Id::speaker, "Mute" }, solo { Icons::Id::headphones, "Solo" },
               edit { Icons::Id::pencil, "Edit notes" }, more { Icons::Id::dots, "More" };
    juce::Slider vol, pan;
    juce::Label name;
};

//==============================================================================
class TrackLanes::Content : public juce::Component
{
public:
    explicit Content (Context& c, TrackLanes& o) : ctx (c), owner (o) {}

    void sync()
    {
        std::vector<int> ids;
        for (auto& t : ctx.session.doc.tracks) ids.push_back (t.id);
        bool same = ids.size() == headers.size();
        for (size_t i = 0; same && i < ids.size(); ++i) same = headers[i]->trackId == ids[i];
        if (! same)
        {
            headers.clear();
            for (int id : ids) { headers.push_back (std::make_unique<LaneHeader> (ctx, id)); addAndMakeVisible (*headers.back()); }
        }
        else for (auto& h : headers) h->refresh();
        setSize (juce::jmax (100, getParentWidth()), juce::jmax ((int) ids.size() * laneH + 40, getParentHeight()));
        resized();
        repaint();
    }

    void resized() override
    {
        for (size_t i = 0; i < headers.size(); ++i) headers[i]->setBounds (0, (int) i * laneH, headerW, laneH);
    }

    juce::Rectangle<float> clipRect (const Track& t) const
    {
        const int idx = ctx.session.doc.indexOf (t.id);
        double len = t.source ? srcToDst (t.warps, t.source->lengthSeconds()) : 1.0;
        if (auto r = ctx.session.result (t.id)) len = r->stereo.getNumSamples() / r->rate;
        const float x0 = (float) ctx.secToX (t.clipStart, (float) headerW);
        const float x1 = (float) ctx.secToX (t.clipStart + len, (float) headerW);
        return { x0, (float) idx * laneH + 6.0f, x1 - x0, (float) laneH - 12.0f };
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) headerW);
        g.setColour (col::bg);
        g.fillRect (area);
        drawGrid (g, ctx, area, (float) headerW);

        auto& doc = ctx.session.doc;
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        for (int i = 0; i < (int) doc.tracks.size(); ++i)
        {
            const auto& t = doc.tracks[(size_t) i];
            g.setColour (col::border.withAlpha (0.6f));
            g.drawHorizontalLine ((i + 1) * laneH - 1, area.getX(), area.getRight());
            if (t.source == nullptr) continue;
            auto cr = clipRect (t);
            if (cr.getRight() < area.getX() || cr.getX() > area.getRight()) continue;

            const bool sel = ctx.ui.selectedTrack == t.id;
            const bool dim = t.mute;
            g.setColour (t.colour.withAlpha (sel ? 0.16f : 0.09f));
            g.fillRoundedRectangle (cr, 6.0f);
            g.setColour (t.colour.withAlpha (sel ? 0.75f : 0.38f));
            g.drawRoundedRectangle (cr.reduced (0.5f), 6.0f, sel ? 1.4f : 1.0f);

            const auto& label = t.name;
            g.setColour (t.colour.withAlpha (0.95f));
            g.setFont (Fonts::semi (12.0f));
            g.drawText (label, cr.reduced (8, 3).removeFromTop (15), juce::Justification::centredLeft, true);

            auto wave = cr.reduced (2.0f, 0).withTrimmedTop (17.0f).withTrimmedBottom (4.0f);
            const bool busy = ctx.session.isBusy (t.id);
            if (auto r = ctx.session.result (t.id))
            {
                const double spp = r->rate / ctx.ui.pxPerSec;
                auto vis = wave.getIntersection (area);
                if (! vis.isEmpty())
                {
                    const double startSample = (vis.getX() - cr.getX()) * spp;
                    drawWaveform (g, r->stereo, vis, startSample, spp, t.colour.withAlpha (dim ? 0.25f : (busy ? 0.35f : 0.85f)));
                }
            }
            if (busy) drawRendering (g, wave, t.colour, owner.animTickValue());

            // warp markers (Time page)
            if (ctx.ui.page == UiState::Time)
                for (auto& w : t.warps)
                {
                    const float x = (float) ctx.secToX (t.clipStart + w.dst, (float) headerW);
                    if (x < cr.getX() || x > cr.getRight()) continue;
                    g.setColour (col::amber.withAlpha (0.85f));
                    g.drawLine (x, cr.getY() + 2, x, cr.getBottom() - 2, 1.2f);
                    juce::Path tri;
                    tri.addTriangle (x - 5, cr.getY() - 3, x + 5, cr.getY() - 3, x, cr.getY() + 5);
                    g.fillPath (tri);
                }
        }
        drawPlayhead (g, ctx, area, (float) headerW);
        g.restoreState();

        if (doc.tracks.empty())
        {
            g.setColour (col::textDim);
            g.setFont (Fonts::displaySemi (20.0f));
            auto msg = area.withSizeKeepingCentre (area.getWidth(), 80);
            g.drawText (ctx.session.pendingImports() > 0 ? "Listening to your take..." : "No vocals yet", msg.removeFromTop (30), juce::Justification::centred);
            g.setFont (Fonts::body (14.0f));
            g.setColour (col::textFaint);
            g.drawText ("Arm Capture on the Import page and press play in FL, or drop a WAV here.", msg, juce::Justification::centred);
        }
    }

    static void drawRendering (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, int tick)
    {
        // drifting ember sparks
        const int cols = (int) (r.getWidth() / 14.0f), rows = juce::jmax (1, (int) (r.getHeight() / 12.0f));
        for (int x = 0; x < cols; ++x)
            for (int y = 0; y < rows; ++y)
            {
                const float n = hashNoise (77, x * 31 + y, tick / 3);
                if (n < 0.1f) continue;
                const float a = 0.15f + 0.6f * n * n;
                const float px = r.getX() + 7.0f + x * 14.0f, py = r.getY() + 6.0f + y * 12.0f;
                g.setColour ((n > 0.85f ? col::amber : c).withAlpha (a));
                if (n > 0.92f) Icons::draw (g, Icons::Id::sparkle, juce::Rectangle<float> (9, 9).withCentre ({ px, py }), col::amber.withAlpha (a));
                else g.fillEllipse (px - 1.2f, py - 1.2f, 2.4f, 2.4f);
            }
        auto pill = juce::Rectangle<float> (128.0f, 24.0f).withCentre (r.getCentre());
        g.setColour (col::bg.withAlpha (0.9f));
        g.fillRoundedRectangle (pill, 12.0f);
        g.setColour (c.withAlpha (0.8f));
        g.drawRoundedRectangle (pill, 12.0f, 1.0f);
        g.setColour (col::text);
        g.setFont (Fonts::semi (12.5f));
        g.drawText ("Rendering voice", pill, juce::Justification::centred);
    }

    // mouse --------------------------------------------------------------------
    int trackAtY (int y) const
    {
        const int i = y / laneH;
        auto& tr = ctx.session.doc.tracks;
        return (i >= 0 && i < (int) tr.size()) ? tr[(size_t) i].id : -1;
    }

    int warpAt (const Track& t, float x) const
    {
        for (int i = 0; i < (int) t.warps.size(); ++i)
            if (std::abs ((float) ctx.secToX (t.clipStart + t.warps[(size_t) i].dst, (float) headerW) - x) < 5.0f) return i;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMode = None;
        if (e.x < headerW) return;
        const int id = trackAtY (e.y);
        if (id < 0) return;
        ctx.select (id);
        auto* t = ctx.session.doc.find (id);
        if (t == nullptr) return;

        if (e.mods.isPopupMenu())
        {
            const int wi = ctx.ui.page == UiState::Time ? warpAt (*t, (float) e.x) : -1;
            if (wi >= 0)
            {
                ctx.session.checkpoint ("Delete warp marker");
                t->warps.erase (t->warps.begin() + wi);
                ctx.session.changed();
            }
            else for (auto& h : headers) if (h->trackId == id) h->showMenu();
            return;
        }

        if (ctx.ui.page == UiState::Time)
        {
            const int wi = warpAt (*t, (float) e.x);
            if (wi >= 0)
            {
                ctx.session.checkpoint ("Move warp marker");
                if (t->isVoice()) t->voice.followParent = false;
                dragMode = Warp; dragIndex = wi; dragTrack = id;
                return;
            }
        }
        if (ctx.ui.tool == UiState::Move && ! t->isVoice())
        {
            ctx.session.checkpoint ("Move clip");
            dragMode = Clip; dragTrack = id; dragStartValue = t->clipStart;
            return;
        }
        if (ctx.ui.tool == UiState::Split || ctx.ui.tool == UiState::Marker)
            addMarkerOrSplit (*t, e.x, ctx.ui.tool == UiState::Marker || ctx.ui.page == UiState::Time);
    }

    void addMarkerOrSplit (Track& t, int x, bool marker)
    {
        const double dst = ctx.xToSec (x, (float) headerW) - t.clipStart;
        const auto w = effectiveWarps (t);
        const double src = dstToSrc (t.warps, dst);
        if (marker)
        {
            ctx.session.checkpoint ("Add warp marker");
            if (t.isVoice()) t.voice.followParent = false;
            Aligner::setWarp (t, src, srcToDst (t.warps, src));
            ctx.session.changed();
            return;
        }
        for (size_t i = 0; i < t.notes.size(); ++i)
        {
            auto& n = t.notes[i];
            if (src > n.srcStart + 0.03 && src < n.srcEnd - 0.03)
            {
                ctx.session.checkpoint ("Split note");
                Note b = n;
                b.id = ctx.session.doc.nextNoteId++;
                b.srcStart = src; n.srcEnd = src;
                t.notes.insert (t.notes.begin() + (long) i + 1, b);
                if (t.isVoice()) t.voice.followParent = false;
                ctx.session.changed();
                return;
            }
        }
        juce::ignoreUnused (w);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* t = ctx.session.doc.find (dragTrack);
        if (t == nullptr) return;
        if (dragMode == Warp && dragIndex < (int) t->warps.size())
        {
            const double dst = ctx.xToSec (e.x, (float) headerW) - t->clipStart;
            const double lo = dragIndex > 0 ? t->warps[(size_t) dragIndex - 1].dst + 0.01 : -10.0;
            const double hi = dragIndex + 1 < (int) t->warps.size() ? t->warps[(size_t) dragIndex + 1].dst - 0.01 : 1e9;
            t->warps[(size_t) dragIndex].dst = juce::jlimit (lo, hi, dst);
            ctx.session.changed();
        }
        else if (dragMode == Clip)
        {
            double v = dragStartValue + e.getDistanceFromDragStartX() / ctx.ui.pxPerSec;
            if (! e.mods.isShiftDown())
            {
                const double grid = ctx.barSeconds() / 16.0;
                v = std::round (v / grid) * grid;
            }
            t->clipStart = juce::jmax (0.0, v);
            ctx.session.changed();
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragMode = None; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.x < headerW) return;
        const int id = trackAtY (e.y);
        auto* t = ctx.session.doc.find (id);
        if (t == nullptr) return;
        if (ctx.ui.page == UiState::Time) addMarkerOrSplit (*t, e.x, true);
        else if (owner.onOpenNotes) owner.onOpenNotes (id);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto cursor = juce::MouseCursor::NormalCursor;
        if (e.x > headerW)
        {
            const int id = trackAtY (e.y);
            if (auto* t = ctx.session.doc.find (id))
            {
                if (ctx.ui.page == UiState::Time && warpAt (*t, (float) e.x) >= 0) cursor = juce::MouseCursor::LeftRightResizeCursor;
                else if (ctx.ui.tool == UiState::Move && ! t->isVoice()) cursor = juce::MouseCursor::DraggingHandCursor;
                else if (ctx.ui.tool == UiState::Split) cursor = juce::MouseCursor::IBeamCursor;
                else if (ctx.ui.tool == UiState::Marker) cursor = juce::MouseCursor::CrosshairCursor;
            }
        }
        setMouseCursor (cursor);
    }

    Context& ctx;
    TrackLanes& owner;
    std::vector<std::unique_ptr<LaneHeader>> headers;
    enum DragMode { None, Warp, Clip } dragMode = None;
    int dragIndex = -1, dragTrack = -1;
    double dragStartValue = 0;
};

//==============================================================================
TrackLanes::TrackLanes (Context& c) : ctx (c)
{
    ruler = std::make_unique<Ruler> (ctx);
    chords = std::make_unique<ChordLane> (ctx);
    content = std::make_unique<Content> (ctx, *this);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (*chords);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    hbar.setAutoHide (false);
    hbar.addListener (this);
    addAndMakeVisible (hbar);
    ctx.session.addChangeListener (this);
    ctx.ui.addChangeListener (this);
    startTimerHz (30);
    refresh();
}

TrackLanes::~TrackLanes()
{
    ctx.session.removeChangeListener (this);
    ctx.ui.removeChangeListener (this);
}

void TrackLanes::resized()
{
    auto r = getLocalBounds();
    ruler->setBounds (r.removeFromTop (rulerH));
    const bool showChords = ctx.ui.page == UiState::Tune;
    chords->setVisible (showChords);
    if (showChords) chords->setBounds (r.removeFromTop (chordH));
    hbar.setBounds (r.removeFromBottom (10).withTrimmedLeft (headerW));
    viewport.setBounds (r);
    content->sync();
}

void TrackLanes::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void TrackLanes::refresh()
{
    const bool showChords = ctx.ui.page == UiState::Tune;
    if (chords->isVisible() != showChords) resized();
    content->sync();
    const double end = ctx.timelineEnd();
    const double visible = (getWidth() - headerW) / ctx.ui.pxPerSec;
    hbar.setRangeLimits (-0.5, juce::jmax (end, visible));
    hbar.setCurrentRange (ctx.ui.scrollSec, visible, juce::dontSendNotification);
    ruler->repaint();
    chords->repaint();
}

void TrackLanes::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void TrackLanes::scrollBarMoved (juce::ScrollBar*, double start)
{
    ctx.ui.scrollSec = start;
    ruler->repaint(); chords->repaint(); content->repaint();
}

void TrackLanes::timerCallback()
{
    ++animTick;
    const double pos = ctx.proc.positionSeconds();
    bool busy = ctx.session.busyCount() > 0;
    if (std::abs (pos - lastPos) > 1e-4 || busy)
    {
        // follow playhead while playing
        if (ctx.proc.isPlaying())
        {
            const double visible = (getWidth() - headerW) / ctx.ui.pxPerSec;
            if (pos > ctx.ui.scrollSec + visible * 0.92 || pos < ctx.ui.scrollSec)
            {
                ctx.ui.scrollSec = pos - visible * 0.1;
                hbar.setCurrentRange (ctx.ui.scrollSec, visible, juce::dontSendNotification);
            }
        }
        lastPos = pos;
        ruler->repaint();
        content->repaint();
        if (busy) for (auto& h : content->headers) h->repaint();
    }
}

void TrackLanes::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double anchor = ctx.xToSec (e.getEventRelativeTo (this).x, (float) headerW);
        ctx.ui.pxPerSec = juce::jlimit (12.0, 1600.0, ctx.ui.pxPerSec * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
        ctx.ui.scrollSec = anchor - (e.getEventRelativeTo (this).x - headerW) / ctx.ui.pxPerSec;
        ctx.ui.notify();
        return;
    }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        ctx.ui.scrollSec = juce::jmax (-0.5, ctx.ui.scrollSec - d * 300.0 / ctx.ui.pxPerSec);
        ctx.ui.notify();
        return;
    }
    viewport.mouseWheelMove (e.getEventRelativeTo (&viewport), w);
}

int TrackLanes::animTickValue() const { return animTick; }
} // namespace hvx::ui
