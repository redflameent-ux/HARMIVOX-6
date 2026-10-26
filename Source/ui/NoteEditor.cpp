#include "NoteEditor.h"
#include "../engine/Aligner.h"
#include "../engine/Analysis.h"

namespace hvx::ui
{
//==============================================================================
class NoteEditor::Grid : public juce::Component, public juce::TooltipClient
{
public:
    explicit Grid (Context& c) : ctx (c) { setWantsKeyboardFocus (true); }

    juce::String getTooltip() override { return {}; }

    float left() const { return (float) keysW; }
    float top() const  { return (float) rulerH; }
    float midiToY (float m) const { return top() + (ctx.ui.noteTopMidi - m) * ctx.ui.pxPerSemi; }
    float yToMidi (float y) const { return ctx.ui.noteTopMidi - (y - top()) / ctx.ui.pxPerSemi; }

    struct Hit { enum Kind { None, Body, EdgeL, EdgeR, Join } kind = None; int index = -1; };

    std::vector<WarpPoint> warpsFor (const Track& t) const { return effectiveWarps (t); }

    juce::Rectangle<float> noteRect (const Track& t, const Note& n, const std::vector<WarpPoint>& w) const
    {
        const float x0 = (float) ctx.secToX (t.clipStart + srcToDst (w, n.srcStart), left());
        const float x1 = (float) ctx.secToX (t.clipStart + srcToDst (w, n.srcEnd), left());
        const float h = ctx.ui.pxPerSemi * 0.86f;
        return { x0, midiToY (n.target) - h * 0.5f, juce::jmax (3.0f, x1 - x0), h };
    }

    std::vector<int> sortedOrder (const Track& t) const
    {
        std::vector<int> idx ((size_t) t.notes.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int) i;
        std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return t.notes[(size_t) a].srcStart < t.notes[(size_t) b].srcStart; });
        return idx;
    }

    /** Join between sorted note k-1 and k: the handle box. */
    bool joinRect (const Track& t, const std::vector<int>& ord, int k, const std::vector<WarpPoint>& w, juce::Rectangle<float>& out) const
    {
        if (k <= 0 || k >= (int) ord.size()) return false;
        const auto& a = t.notes[(size_t) ord[(size_t) k - 1]];
        const auto& b = t.notes[(size_t) ord[(size_t) k]];
        if (b.srcStart - a.srcEnd > 0.12) return false;
        const float x = (float) ctx.secToX (t.clipStart + srcToDst (w, 0.5 * (a.srcEnd + b.srcStart)), left());
        const float y0 = midiToY (a.target), y1 = midiToY (b.target);
        const float h = ctx.ui.pxPerSemi * 0.5f;
        const float tr = b.transition >= 0 ? b.transition : 0.15f;
        const float wdt = juce::jmax (8.0f, (float) ((0.008 + tr * 0.22) * ctx.ui.pxPerSec));
        out = juce::Rectangle<float> (x - wdt * 0.5f, juce::jmin (y0, y1) - h, wdt, std::abs (y1 - y0) + 2 * h);
        return true;
    }

    Hit hitTest (juce::Point<float> p)
    {
        Hit h;
        auto* t = ctx.selected();
        if (t == nullptr) return h;
        const auto w = warpsFor (*t);
        const auto ord = sortedOrder (*t);
        if (ctx.ui.tool == UiState::Select)
            for (int k = 1; k < (int) ord.size(); ++k)
            {
                juce::Rectangle<float> jr;
                if (joinRect (*t, ord, k, w, jr) && jr.expanded (2.0f, 0).contains (p)
                    && std::abs (p.x - jr.getCentreX()) < juce::jmax (5.0f, jr.getWidth() * 0.5f)
                    && ! noteRect (*t, t->notes[(size_t) ord[(size_t) k]], w).reduced (6.0f, 0).contains (p))
                {
                    // only when near the joint itself
                    const auto ra = noteRect (*t, t->notes[(size_t) ord[(size_t) k - 1]], w);
                    const auto rb = noteRect (*t, t->notes[(size_t) ord[(size_t) k]], w);
                    if (! ra.reduced (4, 0).contains (p) || p.x > ra.getRight() - 6)
                        if (! rb.reduced (4, 0).contains (p) || p.x < rb.getX() + 6)
                        { h.kind = Hit::Join; h.index = ord[(size_t) k]; return h; }
                }
            }
        for (int i = (int) t->notes.size() - 1; i >= 0; --i)
        {
            auto r = noteRect (*t, t->notes[(size_t) i], w);
            if (! r.expanded (0, 2).contains (p)) continue;
            h.index = i;
            if (ctx.ui.tool == UiState::Select && r.getWidth() > 18)
            {
                if (p.x < r.getX() + 5) { h.kind = Hit::EdgeL; return h; }
                if (p.x > r.getRight() - 5) { h.kind = Hit::EdgeR; return h; }
            }
            h.kind = Hit::Body;
            return h;
        }
        return h;
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto grid = r.withTrimmedLeft (left()).withTrimmedTop (top());
        g.fillAll (col::bg);

        const auto& doc = ctx.session.doc;
        auto* t = ctx.selected();

        // pitch rows
        const int mTop = (int) std::ceil (ctx.ui.noteTopMidi);
        const int mBot = (int) std::floor (yToMidi (r.getBottom()));
        for (int m = mTop; m >= mBot - 1; --m)
        {
            const float y0 = midiToY ((float) m + 0.5f), y1 = midiToY ((float) m - 0.5f);
            const int pc = ((m % 12) + 12) % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const bool inKey = doc.scale == 9 || inScale (m, doc.key, doc.scale);
            g.setColour (black ? col::bg : col::panel.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (grid.getX(), y0, grid.getWidth(), y1 - y0));
            if (! inKey) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRect (juce::Rectangle<float> (grid.getX(), y0, grid.getWidth(), y1 - y0)); }
            if (pc == 0) { g.setColour (col::borderHi.withAlpha (0.8f)); g.drawHorizontalLine ((int) y1, grid.getX(), grid.getRight()); }

            // keys column
            auto key = juce::Rectangle<float> (0, y0, left() - 1, y1 - y0);
            g.setColour (black ? col::panel : col::panel2);
            g.fillRect (key);
            if (inKey) { g.setColour (col::ember.withAlpha (0.55f)); g.fillEllipse (left() - 9.0f, key.getCentreY() - 2.0f, 4.0f, 4.0f); }
            if (pc == 0)
            {
                g.setColour (col::textDim);
                g.setFont (Fonts::semi (11.5f));
                g.drawText (noteName ((float) m), key.withTrimmedLeft (6).withHeight (juce::jmax (14.0f, key.getHeight())), juce::Justification::centredLeft);
            }
        }
        g.setColour (col::border);
        g.drawVerticalLine ((int) left() - 1, top(), r.getBottom());

        g.saveState();
        g.reduceClipRegion (grid.toNearestInt());
        drawGrid (g, ctx, grid, left());

        if (t != nullptr && t->source != nullptr)
        {
            const auto w = warpsFor (*t);
            const auto ord = sortedOrder (*t);

            // ghost: the lead's notes while editing another track
            if (auto* lead = doc.lead(); lead != nullptr && lead->id != t->id)
            {
                const auto lw = warpsFor (*lead);
                for (auto& n : lead->notes)
                {
                    auto nr = noteRect (*lead, n, lw);
                    g.setColour (lead->colour.withAlpha (0.18f));
                    g.drawRoundedRectangle (nr, 4.0f, 1.0f);
                }
            }

            auto plan = ctx.session.planFor (t->id);

            // original sung pitch (takes only)
            if (plan != nullptr && ! t->isVoice())
            {
                juce::Path orig;
                bool pen = false;
                for (int f = 0; f < plan->size(); f += 1)
                {
                    const float m = plan->inMidi[(size_t) f];
                    if (m <= 0) { pen = false; continue; }
                    const float x = (float) ctx.secToX (t->clipStart + f * plan->dt, left());
                    if (x < grid.getX() - 4 || x > grid.getRight() + 4) { pen = false; continue; }
                    const float y = midiToY (m);
                    if (! pen) { orig.startNewSubPath (x, y); pen = true; } else orig.lineTo (x, y);
                }
                g.setColour (col::textFaint.withAlpha (0.8f));
                g.strokePath (orig, juce::PathStrokeType (1.1f));
            }

            // note blobs
            for (int i = 0; i < (int) t->notes.size(); ++i)
            {
                const auto& n = t->notes[(size_t) i];
                auto nr = noteRect (*t, n, w);
                if (nr.getRight() < grid.getX() || nr.getX() > grid.getRight()) continue;
                const bool sel = ctx.ui.selectedNotes.count (n.id) > 0;
                auto c = n.muted ? col::textFaint : t->colour;
                g.setColour (c.withAlpha (sel ? 0.42f : 0.22f));
                g.fillRoundedRectangle (nr, 4.0f);
                g.setColour (c.withAlpha (sel ? 1.0f : 0.7f));
                g.drawRoundedRectangle (nr.reduced (0.5f), 4.0f, sel ? 1.6f : 1.0f);
                if (sel)
                {
                    g.setColour (c.withAlpha (0.35f));
                    g.drawRoundedRectangle (nr.expanded (2.5f), 6.0f, 2.0f);
                }
                // label
                if (nr.getWidth() > 26 && ctx.ui.pxPerSemi >= 11)
                {
                    g.setColour (col::text.withAlpha (0.8f));
                    g.setFont (Fonts::semi (juce::jmin (11.5f, ctx.ui.pxPerSemi * 0.72f)));
                    const float cents = (n.target - std::round (n.target)) * 100.0f;
                    juce::String lbl = noteName (n.target);
                    if (std::abs (cents) >= 1.0f) lbl << (cents > 0 ? " +" : " ") << juce::roundToInt (cents) << "c";
                    g.drawText (lbl, nr.reduced (4, 0), juce::Justification::centredLeft, true);
                }
            }

            // corrected pitch line
            if (plan != nullptr)
            {
                juce::Path out;
                bool pen = false;
                for (int f = 0; f < plan->size(); ++f)
                {
                    const float m = plan->outMidi[(size_t) f];
                    if (m <= 0 || plan->gain[(size_t) f] < 0.01f) { pen = false; continue; }
                    const float x = (float) ctx.secToX (t->clipStart + f * plan->dt, left());
                    if (x < grid.getX() - 4 || x > grid.getRight() + 4) { pen = false; continue; }
                    const float y = midiToY (m);
                    if (! pen) { out.startNewSubPath (x, y); pen = true; } else out.lineTo (x, y);
                }
                g.setColour (t->colour.brighter (0.6f).withAlpha (0.95f));
                g.strokePath (out, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // joins
            for (int k = 1; k < (int) ord.size(); ++k)
            {
                juce::Rectangle<float> jr;
                if (! joinRect (*t, ord, k, w, jr)) continue;
                const auto& b = t->notes[(size_t) ord[(size_t) k]];
                const bool active = (hoverJoin == ord[(size_t) k]) || (drag == Transition && dragIndex == ord[(size_t) k]);
                if (! active && b.transition < 0) continue;
                g.setColour (col::amber.withAlpha (active ? 0.25f : 0.12f));
                g.fillRoundedRectangle (jr, 3.0f);
                g.setColour (col::amber.withAlpha (active ? 0.95f : 0.5f));
                g.drawRoundedRectangle (jr, 3.0f, 1.2f);
                if (active)
                {
                    const juce::String s = "Transition " + (b.transition < 0 ? juce::String ("auto") : formatPct (b.transition));
                    auto lr = juce::Rectangle<float> (jr.getRight() + 8, jr.getY() - 2, 110, 24);
                    g.setColour (col::bg.withAlpha (0.92f));
                    g.fillRoundedRectangle (lr, 6.0f);
                    g.setColour (col::amber);
                    g.drawRoundedRectangle (lr, 6.0f, 1.0f);
                    g.setColour (col::text);
                    g.setFont (Fonts::semi (12.5f));
                    g.drawText (s, lr, juce::Justification::centred);
                }
            }
        }
        else
        {
            g.setColour (col::textDim);
            g.setFont (Fonts::displaySemi (20.0f));
            g.drawText ("Pick a track to edit its notes", grid, juce::Justification::centred);
        }

        // drift / cents readout while dragging
        if (dragLabel.isNotEmpty())
        {
            auto lr = juce::Rectangle<float> (lastMouse.x + 14, lastMouse.y - 30, 130, 24);
            g.setColour (col::bg.withAlpha (0.92f));
            g.fillRoundedRectangle (lr, 6.0f);
            g.setColour (col::borderHi);
            g.drawRoundedRectangle (lr, 6.0f, 1.0f);
            g.setColour (col::text);
            g.setFont (Fonts::semi (12.5f));
            g.drawText (dragLabel, lr, juce::Justification::centred);
        }

        if (drag == Box)
        {
            g.setColour (col::ember.withAlpha (0.08f));
            g.fillRect (boxRect);
            g.setColour (col::ember.withAlpha (0.6f));
            g.drawRect (boxRect, 1.0f);
        }
        drawPlayhead (g, ctx, grid, left());
        g.restoreState();

        // ruler
        auto rr = r.withHeight (top()).withTrimmedLeft (left());
        g.setColour (col::panel);
        g.fillRect (r.withHeight (top()));
        g.saveState();
        g.reduceClipRegion (rr.toNearestInt());
        drawRuler (g, ctx, rr, left());
        const float px = (float) ctx.secToX (ctx.proc.positionSeconds(), left());
        juce::Path tri; tri.addTriangle (px - 6, 2, px + 6, 2, px, 11);
        g.setColour (col::ember); g.fillPath (tri);
        g.restoreState();
        g.setColour (col::textFaint);
        g.setFont (Fonts::semi (11.0f));
        g.drawText (keyNames()[doc.key] + " " + (doc.scale == 0 ? "maj" : doc.scale == 1 ? "min" : scaleNames()[doc.scale].substring (0, 4).toLowerCase()),
                    juce::Rectangle<float> (0, 0, left(), top()), juce::Justification::centred);
        g.setColour (col::border);
        g.drawHorizontalLine ((int) top() - 1, 0, r.getRight());
    }

    //==========================================================================
    void markEdited (Track& t)
    {
        if (t.isVoice()) t.voice.followParent = false;
    }

    void anchorAt (Track& t, double src)
    {
        for (auto& p : t.warps) if (std::abs (p.src - src) < 0.002) return;
        t.warps.push_back ({ src, srcToDst (t.warps, src) });
        Aligner::tidy (t.warps);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        lastMouse = e.position;
        drag = None;
        dragLabel.clear();
        auto* t = ctx.selected();

        if (e.y < top())
        {
            ctx.proc.setInternalPosition (juce::jmax (0.0, ctx.xToSec (e.x, left())));
            drag = Seek;
            return;
        }
        if (t == nullptr || e.x < left()) return;

        const auto hit = hitTest (e.position);
        rightDrag = e.mods.isPopupMenu();

        if (ctx.ui.tool == UiState::Split && hit.kind != Hit::None && hit.kind != Hit::Join)
        {
            splitAt (*t, hit.index, e.x);
            return;
        }

        if (hit.kind == Hit::Join)
        {
            ctx.session.checkpoint ("Transition");
            markEdited (*t);
            drag = Transition; dragIndex = hit.index;
            startValue = t->notes[(size_t) hit.index].transition < 0 ? 0.15f : t->notes[(size_t) hit.index].transition;
            return;
        }

        if (hit.kind == Hit::None)
        {
            if (! e.mods.isShiftDown()) ctx.ui.selectedNotes.clear();
            if (rightDrag) { showMenu (-1, e); return; }
            drag = Box; boxStart = e.position; boxRect = {};
            ctx.ui.notify();
            return;
        }

        auto& n = t->notes[(size_t) hit.index];
        if (! ctx.ui.selectedNotes.count (n.id))
        {
            if (! e.mods.isShiftDown()) ctx.ui.selectedNotes.clear();
            ctx.ui.selectedNotes.insert (n.id);
            ctx.ui.notify();
        }
        dragIndex = hit.index;

        if (rightDrag)
        {
            drag = DriftPending;
            startValue = n.driftFix >= 0 ? n.driftFix : t->tune.driftFix;
            return;
        }

        startTargets.clear();
        for (auto& x : t->notes) if (ctx.ui.selectedNotes.count (x.id)) startTargets[x.id] = x.target;
        baseWarps = t->warps;

        if (hit.kind == Hit::EdgeL || hit.kind == Hit::EdgeR)
        {
            ctx.session.checkpoint ("Note timing");
            markEdited (*t);
            drag = hit.kind == Hit::EdgeL ? EdgeL : EdgeR;
            startDst = srcToDst (t->warps, hit.kind == Hit::EdgeL ? n.srcStart : n.srcEnd);
            return;
        }
        if (ctx.ui.tool == UiState::Move)
        {
            ctx.session.checkpoint ("Move note");
            markEdited (*t);
            drag = MoveTime;
            startDst = srcToDst (t->warps, n.srcStart);
            return;
        }
        drag = PitchPending;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        lastMouse = e.position;
        auto* t = ctx.selected();
        if (drag == Seek) { ctx.proc.setInternalPosition (juce::jmax (0.0, ctx.xToSec (e.x, left()))); repaint(); return; }
        if (t == nullptr) return;
        const float dy = (float) e.getDistanceFromDragStartY();
        const float dx = (float) e.getDistanceFromDragStartX();

        if (drag == PitchPending && (std::abs (dy) > 3 || std::abs (dx) > 3))
        {
            ctx.session.checkpoint ("Pitch");
            markEdited (*t);
            drag = Pitch;
        }
        if (drag == DriftPending && std::abs (dy) > 3)
        {
            ctx.session.checkpoint ("Drift fix");
            markEdited (*t);
            drag = Drift;
        }

        switch (drag)
        {
            case Pitch:
            {
                float delta = -dy / ctx.ui.pxPerSemi;
                const bool fine = e.mods.isAltDown();
                for (auto& n : t->notes)
                {
                    auto it = startTargets.find (n.id);
                    if (it == startTargets.end()) continue;
                    float v = it->second + delta;
                    if (! fine) v = std::round (v);
                    n.target = juce::jlimit (24.0f, 96.0f, v);
                    n.edited = true;
                }
                auto& n0 = t->notes[(size_t) dragIndex];
                const float cents = (n0.target - std::round (n0.target)) * 100.0f;
                dragLabel = noteName (n0.target) + (fine ? "  " + juce::String (juce::roundToInt (cents)) + " cents" : juce::String());
                ctx.session.changed();
                break;
            }
            case Drift:
            {
                auto v = (float) juce::jlimit (0.0, 1.0, startValue - dy / 150.0);
                for (auto& n : t->notes) if (ctx.ui.selectedNotes.count (n.id)) n.driftFix = v;
                dragLabel = "Drift fix " + formatPct (v);
                ctx.session.changed();
                break;
            }
            case Transition:
            {
                auto v = (float) juce::jlimit (0.0, 1.0, startValue + (rightDrag ? -dy / 150.0 : dx / 150.0));
                t->notes[(size_t) dragIndex].transition = v;
                ctx.session.changed();
                break;
            }
            case EdgeL:
            case EdgeR:
            case MoveTime:
            {
                t->warps = baseWarps;
                const auto ord = sortedOrder (*t);
                int k = 0;
                for (int i = 0; i < (int) ord.size(); ++i) if (ord[(size_t) i] == dragIndex) k = i;
                const auto& n = t->notes[(size_t) dragIndex];
                const Note* prev = k > 0 ? &t->notes[(size_t) ord[(size_t) k - 1]] : nullptr;
                const Note* next = k + 1 < (int) ord.size() ? &t->notes[(size_t) ord[(size_t) k + 1]] : nullptr;
                double delta = dx / ctx.ui.pxPerSec;
                if (! e.mods.isShiftDown() && drag == MoveTime)
                {
                    const double grid = ctx.barSeconds() / 16.0;
                    const double host = t->clipStart + startDst + delta;
                    delta = std::round (host / grid) * grid - t->clipStart - startDst;
                }
                if (drag == EdgeL)
                {
                    if (prev) anchorAt (*t, prev->srcStart);
                    anchorAt (*t, n.srcEnd);
                    const double lo = prev ? srcToDst (t->warps, prev->srcStart) + 0.02 : -5.0;
                    const double hi = srcToDst (t->warps, n.srcEnd) - 0.02;
                    Aligner::setWarp (*t, n.srcStart, juce::jlimit (lo, hi, startDst + delta));
                }
                else if (drag == EdgeR)
                {
                    anchorAt (*t, n.srcStart);
                    if (next) anchorAt (*t, next->srcEnd);
                    const double lo = srcToDst (t->warps, n.srcStart) + 0.02;
                    const double hi = next ? srcToDst (t->warps, next->srcEnd) - 0.02 : 1e6;
                    Aligner::setWarp (*t, n.srcEnd, juce::jlimit (lo, hi, startDst + delta));
                }
                else
                {
                    if (prev) anchorAt (*t, prev->srcStart);
                    if (next) anchorAt (*t, next->srcEnd);
                    const double len = srcToDst (t->warps, n.srcEnd) - srcToDst (t->warps, n.srcStart);
                    const double lo = prev ? srcToDst (t->warps, prev->srcStart) + 0.02 : -5.0;
                    const double hi = next ? srcToDst (t->warps, next->srcEnd) - 0.02 - len : 1e6;
                    const double ns = juce::jlimit (lo, hi, startDst + delta);
                    Aligner::setWarp (*t, n.srcStart, ns);
                    Aligner::setWarp (*t, n.srcEnd, ns + len);
                }
                ctx.session.changed();
                break;
            }
            case Box:
            {
                boxRect = juce::Rectangle<float> (boxStart, e.position);
                const auto w = warpsFor (*t);
                std::set<int> sel;
                for (auto& n : t->notes) if (noteRect (*t, n, w).intersects (boxRect)) sel.insert (n.id);
                ctx.ui.selectedNotes = sel;
                ctx.ui.notify();
                break;
            }
            default: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* t = ctx.selected();
        if (drag == DriftPending && t != nullptr) showMenu (dragIndex, e);
        drag = None;
        dragLabel.clear();
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = hitTest (e.position);
        const int hj = h.kind == Hit::Join ? h.index : -1;
        if (hj != hoverJoin) { hoverJoin = hj; repaint(); }
        auto cur = juce::MouseCursor::NormalCursor;
        if (ctx.ui.tool == UiState::Split && h.kind == Hit::Body) cur = juce::MouseCursor::IBeamCursor;
        else if (h.kind == Hit::EdgeL || h.kind == Hit::EdgeR || h.kind == Hit::Join) cur = juce::MouseCursor::LeftRightResizeCursor;
        else if (h.kind == Hit::Body) cur = ctx.ui.tool == UiState::Move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::UpDownResizeCursor;
        setMouseCursor (cur);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto* t = ctx.selected();
        if (t == nullptr) return;
        const auto h = hitTest (e.position);
        if (h.kind == Hit::Body)
        {
            ctx.session.checkpoint ("Snap note");
            auto& n = t->notes[(size_t) h.index];
            n.target = (float) snapToScale (n.detected, ctx.session.doc.key, ctx.session.doc.scale);
            n.edited = false;
            markEdited (*t);
            ctx.session.changed();
        }
        else if (h.kind == Hit::Join)
        {
            ctx.session.checkpoint ("Transition auto");
            t->notes[(size_t) h.index].transition = -1.0f;
            ctx.session.changed();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            const double anchor = ctx.xToSec (e.x, left());
            ctx.ui.pxPerSec = juce::jlimit (12.0, 2400.0, ctx.ui.pxPerSec * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            ctx.ui.scrollSec = anchor - (e.x - left()) / ctx.ui.pxPerSec;
        }
        else if (e.mods.isAltDown())
        {
            const float anchor = yToMidi ((float) e.y);
            ctx.ui.pxPerSemi = juce::jlimit (6.0f, 40.0f, ctx.ui.pxPerSemi * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f));
            ctx.ui.noteTopMidi = anchor + (e.y - top()) / ctx.ui.pxPerSemi;
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            ctx.ui.scrollSec = juce::jmax (-0.5, ctx.ui.scrollSec - d * 300.0 / ctx.ui.pxPerSec);
        }
        else
            ctx.ui.noteTopMidi = juce::jlimit (40.0f, 108.0f, ctx.ui.noteTopMidi + w.deltaY * 12.0f);
        ctx.ui.notify();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto* t = ctx.selected();
        if (t == nullptr) return false;
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            ctx.ui.selectedNotes.clear();
            for (auto& n : t->notes) ctx.ui.selectedNotes.insert (n.id);
            ctx.ui.notify();
            return true;
        }
        if (ctx.ui.selectedNotes.empty()) return false;
        if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)
        {
            const float step = (k.getModifiers().isAltDown() ? 0.1f : 1.0f) * (k.getKeyCode() == juce::KeyPress::upKey ? 1.0f : -1.0f);
            ctx.session.checkpoint ("Pitch");
            markEdited (*t);
            for (auto& n : t->notes) if (ctx.ui.selectedNotes.count (n.id)) { n.target += step; n.edited = true; }
            ctx.session.changed();
            return true;
        }
        if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)
        {
            ctx.session.checkpoint ("Mute notes");
            markEdited (*t);
            for (auto& n : t->notes) if (ctx.ui.selectedNotes.count (n.id)) n.muted = ! n.muted;
            ctx.session.changed();
            return true;
        }
        return false;
    }

    void splitAt (Track& t, int index, int x)
    {
        const auto w = warpsFor (t);
        const double dst = ctx.xToSec (x, left()) - t.clipStart;
        const double src = dstToSrc (w, dst);
        auto& n = t.notes[(size_t) index];
        if (src <= n.srcStart + 0.03 || src >= n.srcEnd - 0.03) return;
        ctx.session.checkpoint ("Split note");
        markEdited (t);
        Note b = n;
        b.id = ctx.session.doc.nextNoteId++;
        b.srcStart = src;
        b.transition = 0.05f;
        n.srcEnd = src;
        // re-measure both halves
        auto measure = [&] (Note& m)
        {
            std::vector<float> ps;
            for (double s = m.srcStart; s < m.srcEnd; s += 0.005) { float p; if (t.source->pitchAt (s, p)) ps.push_back (p); }
            if (! ps.empty()) { const float shift = m.target - m.detected; m.detected = Analysis::medianOf (ps); if (! t.isVoice()) m.target = m.edited ? m.detected + shift : (float) snapToScale (m.detected, ctx.session.doc.key, ctx.session.doc.scale); }
        };
        measure (n); measure (b);
        t.notes.insert (t.notes.begin() + index + 1, b);
        ctx.session.changed();
    }

    void mergeWithNext (Track& t, int index)
    {
        const auto ord = sortedOrder (t);
        for (int k = 0; k + 1 < (int) ord.size(); ++k)
            if (ord[(size_t) k] == index)
            {
                ctx.session.checkpoint ("Merge notes");
                markEdited (t);
                auto& a = t.notes[(size_t) index];
                const auto b = t.notes[(size_t) ord[(size_t) k + 1]];
                const double la = a.srcEnd - a.srcStart, lb = b.srcEnd - b.srcStart;
                a.detected = (float) ((a.detected * la + b.detected * lb) / juce::jmax (1e-6, la + lb));
                a.srcEnd = b.srcEnd;
                t.notes.erase (t.notes.begin() + ord[(size_t) k + 1]);
                ctx.ui.selectedNotes.clear();
                ctx.session.changed();
                return;
            }
    }

    void showMenu (int index, const juce::MouseEvent& e)
    {
        auto* t = ctx.selected();
        if (t == nullptr) return;
        juce::PopupMenu m;
        const bool onNote = index >= 0 && index < (int) t->notes.size();
        m.addItem (1, "Snap to scale", onNote);
        m.addItem (2, "Back to what I sang", onNote);
        m.addItem (3, onNote && t->notes[(size_t) index].muted ? "Unmute note" : "Mute note", onNote);
        m.addItem (4, "Split here", onNote);
        m.addItem (5, "Merge with next note", onNote);
        m.addItem (6, "Reset note settings", onNote);
        m.addSeparator();
        m.addItem (7, "Select all notes");
        if (t->isVoice()) m.addItem (8, "Re-sync this voice to the lead", ! t->voice.followParent);
        const int x = e.x;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, index, x] (int r)
        {
            auto* tr = ctx.selected();
            if (tr == nullptr || r == 0) return;
            if (r == 7) { ctx.ui.selectedNotes.clear(); for (auto& n : tr->notes) ctx.ui.selectedNotes.insert (n.id); ctx.ui.notify(); return; }
            if (r == 8) { ctx.session.resyncVoice (tr->id); return; }
            if (index < 0 || index >= (int) tr->notes.size()) return;
            if (r == 4) { splitAt (*tr, index, x); return; }
            if (r == 5) { mergeWithNext (*tr, index); return; }
            ctx.session.checkpoint ("Note edit");
            markEdited (*tr);
            for (auto& n : tr->notes)
            {
                if (! ctx.ui.selectedNotes.count (n.id) && n.id != tr->notes[(size_t) index].id) continue;
                if (r == 1) { n.target = (float) snapToScale (n.detected, ctx.session.doc.key, ctx.session.doc.scale); n.edited = false; }
                if (r == 2) { n.target = n.detected; n.edited = true; }
                if (r == 3) n.muted = ! n.muted;
                if (r == 6) { const Note fresh; n.volumeDb = 0; n.formantSt = 0; n.driftFix = -1; n.vibrato = -1; n.addVibratoCents = 0; n.vibratoRateHz = fresh.vibratoRateHz; n.transition = -1; n.muted = false; }
            }
            ctx.session.changed();
        });
    }

    Context& ctx;
    enum Drag { None, Seek, PitchPending, Pitch, DriftPending, Drift, Transition, EdgeL, EdgeR, MoveTime, Box } drag = None;
    int dragIndex = -1, hoverJoin = -1;
    bool rightDrag = false;
    float startValue = 0;
    double startDst = 0;
    std::map<int, float> startTargets;
    std::vector<WarpPoint> baseWarps;
    juce::Point<float> boxStart, lastMouse;
    juce::Rectangle<float> boxRect;
    juce::String dragLabel;
};

//==============================================================================
class NoteEditor::NoteBar : public juce::Component
{
public:
    explicit NoteBar (Context& c) : ctx (c)
    {
        vol.slider.getProperties().set ("bipolar", true);
        formant.setBipolar (true);
        for (auto* k : { &vol, &formant, &drift, &vib, &addVib, &rate, &transitions }) addAndMakeVisible (k);

        auto bind = [this] (Knob& k, const char* label, std::function<void (Note&, double)> set)
        {
            k.onDragStart = [this, label] { ctx.session.checkpoint (label); if (auto* t = ctx.selected()) if (t->isVoice()) t->voice.followParent = false; };
            k.onChange = [this, set] (double v)
            {
                if (updating) return;
                auto* t = ctx.selected();
                if (t == nullptr) return;
                for (auto& n : t->notes) if (ctx.ui.selectedNotes.count (n.id)) set (n, v);
                ctx.session.changed();
            };
        };
        bind (vol, "Note volume", [] (Note& n, double v) { n.volumeDb = (float) v; });
        bind (formant, "Formant", [] (Note& n, double v) { n.formantSt = (float) v; });
        bind (drift, "Drift fix", [] (Note& n, double v) { n.driftFix = (float) v; });
        bind (vib, "Vibrato", [] (Note& n, double v) { n.vibrato = (float) v; });
        bind (addVib, "Add vibrato", [] (Note& n, double v) { n.addVibratoCents = (float) v; });
        bind (rate, "Vibrato rate", [] (Note& n, double v) { n.vibratoRateHz = (float) v; });

        transitions.onDragStart = [this] { ctx.session.checkpoint ("Transitions"); };
        transitions.onChange = [this] (double v)
        {
            if (updating) return;
            for (auto& t : ctx.session.doc.tracks)
            {
                if (t.isVoice() && t.voice.followParent) continue;
                for (auto& n : t.notes) n.transition = v < 0.005 ? -1.0f : (float) v;
            }
            ctx.session.changed();
        };
        transitions.setTooltip ("Sets every join on every track. 0% = automatic (uses Retune speed).");

        for (auto* b : { &muteBtn, &resetBtn }) { styleButton (*b, "secondary"); addAndMakeVisible (b); }
        muteBtn.onClick = [this]
        {
            auto* t = ctx.selected(); if (t == nullptr) return;
            ctx.session.checkpoint ("Mute notes");
            if (t->isVoice()) t->voice.followParent = false;
            for (auto& n : t->notes) if (ctx.ui.selectedNotes.count (n.id)) n.muted = ! n.muted;
            ctx.session.changed();
        };
        resetBtn.onClick = [this]
        {
            auto* t = ctx.selected(); if (t == nullptr) return;
            ctx.session.checkpoint ("Reset notes");
            const Note fresh;
            for (auto& n : t->notes)
                if (ctx.ui.selectedNotes.count (n.id))
                {
                    n.volumeDb = 0; n.formantSt = 0; n.driftFix = -1; n.vibrato = -1; n.addVibratoCents = 0;
                    n.vibratoRateHz = fresh.vibratoRateHz; n.transition = -1; n.muted = false;
                    if (! t->isVoice()) { n.target = (float) snapToScale (n.detected, ctx.session.doc.key, ctx.session.doc.scale); n.edited = false; }
                }
            ctx.session.changed();
        };
    }

    void refresh()
    {
        auto* t = ctx.selected();
        const Note* first = nullptr;
        int count = 0;
        if (t) for (auto& n : t->notes) if (ctx.ui.selectedNotes.count (n.id)) { if (! first) first = &n; ++count; }
        selCount = count;
        updating = true;
        for (auto* k : { &vol, &formant, &drift, &vib, &addVib, &rate }) k->setEnabled (count > 0);
        muteBtn.setEnabled (count > 0); resetBtn.setEnabled (count > 0);
        if (first)
        {
            vol.setValue (first->volumeDb); formant.setValue (first->formantSt);
            drift.setValue (first->driftFix >= 0 ? first->driftFix : t->tune.driftFix);
            vib.setValue (first->vibrato >= 0 ? first->vibrato : t->tune.vibrato);
            addVib.setValue (first->addVibratoCents); rate.setValue (first->vibratoRateHz);
            muteBtn.setButtonText (first->muted ? "Unmute" : "Mute");
        }
        float tr = -1;
        if (t && ! t->notes.empty()) tr = t->notes.front().transition;
        transitions.setValue (tr < 0 ? 0.0 : tr);
        updating = false;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 8);
        r.removeFromTop (22);
        auto right = r.removeFromRight (90);
        auto btns = r.removeFromRight (100).reduced (6, 4);
        muteBtn.setBounds (btns.removeFromTop (btns.getHeight() / 2).reduced (0, 3));
        resetBtn.setBounds (btns.reduced (0, 3));
        transitions.setBounds (right.withTrimmedLeft (12));
        for (auto* k : { &vol, &formant, &drift, &vib, &addVib, &rate }) k->setBounds (r.removeFromLeft (84));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::border);
        g.drawHorizontalLine (0, 0, (float) getWidth());
        g.setColour (col::text);
        g.setFont (Fonts::semi (13.5f));
        g.drawText (selCount == 0 ? juce::String ("No notes selected") : juce::String (selCount) + (selCount == 1 ? " note selected" : " notes selected"),
                    16, 8, 220, 18, juce::Justification::centredLeft);
        g.setColour (col::textFaint);
        g.setFont (Fonts::body (12.5f));
        g.drawText ("Drag: pitch  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  Alt-drag: cents  "
                    + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  Right-drag: drift, on a join: transition  "
                    + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  Double-click: snap",
                    240, 8, getWidth() - 360, 18, juce::Justification::centredRight);
        drawSectionLabel (g, "All tracks", juce::Rectangle<int> (getWidth() - 92, 8, 90, 18));
        g.setColour (col::border);
        g.drawVerticalLine (getWidth() - 102, 30, (float) getHeight() - 10);
    }

    Context& ctx;
    bool updating = false;
    int selCount = 0;
    Knob vol { "Volume", -24.0, 12.0, 0.0, [] (double v) { return formatDb (v); } };
    Knob formant { "Formant", -6.0, 6.0, 0.0, [] (double v) { return juce::String (v, 1) + " st"; } };
    Knob drift { "Drift fix", 0.0, 1.0, 0.0, formatPct };
    Knob vib { "Vibrato", 0.0, 1.5, 1.0, formatPct };
    Knob addVib { "Add vibrato", 0.0, 120.0, 0.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " ct"; } };
    Knob rate { "Rate", 3.0, 8.0, 5.5, [] (double v) { return juce::String (v, 1) + " Hz"; } };
    Knob transitions { "Transitions", 0.0, 1.0, 0.0, [] (double v) { return v < 0.005 ? juce::String ("auto") : formatPct (v); } };
    juce::TextButton muteBtn { "Mute" }, resetBtn { "Reset" };
};

//==============================================================================
NoteEditor::NoteEditor (Context& c) : ctx (c)
{
    grid = std::make_unique<Grid> (ctx);
    bar = std::make_unique<NoteBar> (ctx);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*bar);
    ctx.session.addChangeListener (this);
    ctx.ui.addChangeListener (this);
    startTimerHz (30);
}

NoteEditor::~NoteEditor()
{
    ctx.session.removeChangeListener (this);
    ctx.ui.removeChangeListener (this);
}

void NoteEditor::resized()
{
    auto r = getLocalBounds();
    bar->setBounds (r.removeFromBottom (barH));
    grid->setBounds (r);
}

void NoteEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void NoteEditor::centreOnTrack()
{
    auto* t = ctx.selected();
    if (t == nullptr || t->notes.empty()) return;
    std::vector<float> ps;
    for (auto& n : t->notes) ps.push_back (n.target);
    const float med = Analysis::medianOf (ps);
    const float rows = (grid->getHeight() - rulerH) / ctx.ui.pxPerSemi;
    ctx.ui.noteTopMidi = med + rows * 0.5f;
    const double first = t->clipStart + srcToDst (effectiveWarps (*t), t->notes.front().srcStart);
    const double visible = (grid->getWidth() - keysW) / ctx.ui.pxPerSec;
    if (first < ctx.ui.scrollSec || first > ctx.ui.scrollSec + visible) ctx.ui.scrollSec = first - 0.3;
}

void NoteEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (ctx.ui.selectedTrack != lastTrack && isVisible())
    {
        lastTrack = ctx.ui.selectedTrack;
        centreOnTrack();
    }
    bar->refresh();
    grid->repaint();
}

void NoteEditor::timerCallback()
{
    if (isVisible() && (ctx.proc.isPlaying() || ctx.session.busyCount() > 0)) grid->repaint();
}
} // namespace hvx::ui
