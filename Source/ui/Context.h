#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace hvx::ui
{
/** Everything the editor's components share. */
struct Context
{
    HarmivoxProcessor& proc;
    Session& session;
    UiState& ui;

    std::function<void()> openAddVoices;
    std::function<void()> openVoiceLab;
    std::function<void (const juce::String& title, const juce::String& body, juce::Component* near)> showHelp;
    std::function<void (const juce::String&)> toast;

    Track* selected()
    {
        if (auto* t = session.doc.find (ui.selectedTrack)) return t;
        if (auto* l = session.doc.lead()) { ui.selectedTrack = l->id; return l; }
        return nullptr;
    }

    void select (int id)
    {
        if (ui.selectedTrack != id) ui.selectedNotes.clear();
        ui.selectedTrack = id;
        ui.notify();
    }

    /** Timeline end (seconds) across all tracks, for scrolling limits. */
    double timelineEnd() const
    {
        double e = 8.0;
        for (auto& t : session.doc.tracks)
            if (t.source != nullptr)
                e = juce::jmax (e, t.clipStart + srcToDst (t.warps, t.source->lengthSeconds()) + 2.0);
        return e;
    }

    double secToX (double sec, float left) const { return left + (sec - ui.scrollSec) * ui.pxPerSec; }
    double xToSec (double x, float left) const   { return ui.scrollSec + (x - left) / ui.pxPerSec; }

    double barSeconds() const { return session.doc.beatsPerBar * 60.0 / juce::jmax (20.0, session.doc.bpm); }
};

/** Draws bar numbers and beat ticks. */
void drawRuler (juce::Graphics& g, Context& ctx, juce::Rectangle<float> r, float left);
/** Bar and beat grid lines behind lanes / notes. */
void drawGrid (juce::Graphics& g, Context& ctx, juce::Rectangle<float> r, float left);
/** Playhead line. */
void drawPlayhead (juce::Graphics& g, Context& ctx, juce::Rectangle<float> r, float left);
} // namespace hvx::ui
