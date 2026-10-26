// Development-only: when the HVX_SNAPSHOT_DIR environment variable is set, the
// standalone app walks through every page and saves screenshots there, then quits.
#include "DevSnapshot.h"
#include "MainEditor.h"
#include <iostream>

using namespace hvx;

static juce::AudioBuffer<float> singDemo (double sr)
{
    struct N { double s, l; float m; };
    // a short minor phrase, sung a little loose
    std::vector<N> notes { { 0.10, 0.42, 57.3f }, { 0.55, 0.40, 60.2f }, { 0.98, 0.70, 62.15f }, { 1.72, 0.36, 60.1f },
                           { 2.10, 0.95, 64.3f }, { 3.40, 0.40, 62.0f }, { 3.82, 0.42, 60.3f }, { 4.27, 0.80, 59.8f },
                           { 5.10, 0.38, 57.2f }, { 5.50, 1.30, 55.1f } };
    juce::AudioBuffer<float> b (1, (int) (7.4 * sr));
    b.clear();
    float* x = b.getWritePointer (0);
    juce::Random rng (5);
    for (auto& n : notes)
    {
        struct Res { double f, bw, g, y1 = 0, y2 = 0; };
        Res res[] = { { 650, 90, 1.0 }, { 1080, 110, 0.5 }, { 2650, 160, 0.25 }, { 3500, 250, 0.1 } };
        double ph = 0;
        const int s0 = (int) (n.s * sr), s1 = (int) ((n.s + n.l) * sr);
        for (int i = s0; i < s1 && i < b.getNumSamples(); ++i)
        {
            const double t = (i - s0) / sr;
            const double vib = 0.35 * std::sin (2 * juce::MathConstants<double>::pi * 5.4 * t) * juce::jmin (1.0, t / 0.35);
            const double scoop = -0.6 * std::exp (-t / 0.05);
            ph += 440.0 * std::pow (2.0, (n.m + vib + scoop - 69.0) / 12.0) / sr;
            double pulse = 0; if (ph >= 1) { ph -= 1; pulse = 1; }
            const double src = pulse + 0.012 * (rng.nextDouble() - 0.5);
            double out = 0;
            for (auto& r : res)
            {
                const double R = std::exp (-juce::MathConstants<double>::pi * r.bw / sr);
                const double c = 2 * R * std::cos (2 * juce::MathConstants<double>::pi * r.f / sr);
                const double y = src * (1 - R) * r.g * 8 + c * r.y1 - R * R * r.y2;
                r.y2 = r.y1; r.y1 = y; out += y;
            }
            const double env = juce::jmin (1.0, t / 0.02) * juce::jmin (1.0, (n.l - t) / 0.04);
            x[i] += (float) (out * env * 0.3);
        }
    }
    return b;
}


DevSnapshot::DevSnapshot (HarmivoxEditor& e, HarmivoxProcessor& p, juce::File dir)
    : editor (e), proc (p), outDir (dir)
{
    outDir.createDirectory();
    startTimer (250);
}

std::unique_ptr<DevSnapshot> DevSnapshot::createIfRequested (HarmivoxEditor& e, HarmivoxProcessor& p)
{
    auto dir = juce::SystemStats::getEnvironmentVariable ("HVX_SNAPSHOT_DIR", {});
    if (dir.isEmpty()) return nullptr;
    return std::make_unique<DevSnapshot> (e, p, juce::File (dir));
}

void DevSnapshot::snap (const juce::String& name)
{
    auto img = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    auto f = outDir.getChildFile (name + ".png");
    f.deleteFile();
    if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    std::cout << "wrote " << f.getFullPathName() << std::endl;
}

void DevSnapshot::finish()
{
    stopTimer();
    if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
}

void DevSnapshot::timerCallback()
{
    auto& s = proc.session;
    auto idle = [&] { return s.busyCount() == 0; };
    ++ticks;
    if (ticks > 400) { std::cout << "timeout at step " << step << std::endl; finish(); return; }
    switch (step)
    {
        case 0: editor.setSize (1440, 900); snap ("00_empty"); s.doc.bpm = 96.0; s.importAudio (singDemo (proc.currentSampleRate()), proc.currentSampleRate(), "Take 1", 0.0); step++; break;
        case 1: if (! s.doc.tracks.empty() && idle()) { editor.setPage (2); step++; } break;
        case 2:
        {
            s.checkpoint ("Add voices");
            AddVoicesOptions o;
            o.parentId = s.doc.lead()->id;
            o.voices = { { 0, 2, -5.0f }, { 2, 2, -8.0f }, { -2, 2, -9.0f }, { 4, 1, -10.0f } };
            s.addVoices (o);
            step++; ticks = 0; break;
        }
        case 3: if (ticks == 2) snap ("01_rendering"); if (idle() && ticks > 3) { proc.setInternalPosition (2.4); step++; } break;
        case 4: snap ("02_tune_lanes"); editor.uiState().selectedTrack = s.doc.lead()->id; editor.setNotesView (true); step++; break;
        case 5:
        {
            auto* t = s.doc.lead();
            editor.uiState().selectedNotes = { t->notes[2].id };
            editor.uiState().notify();
            step++; break;
        }
        case 6: snap ("03_notes"); editor.setNotesView (false); editor.openAddVoices(); step++; break;
        case 7: snap ("04_add_voices"); editor.closeOverlays(); editor.setPage (3); step++; break;
        case 8: s.quantize (s.doc.lead()->id, 16, 0.5f); step++; ticks = 0; break;
        case 9: if (idle() && ticks > 2) { snap ("05_time"); editor.setPage (4); step++; } break;
        case 10: snap ("06_finish"); editor.setPage (0); step++; break;
        case 11: snap ("07_import"); editor.setPage (1); step++; break;
        case 12: snap ("08_clean"); editor.openVoiceLab(); step++; break;
        case 13: snap ("09_voicelab"); editor.openVoiceSession(); step++; break;
        case 14: snap ("10_voicelab_session"); editor.closeOverlays(); step++; break;
        case 15:
        {
            auto files = s.exportForDrag (true);
            std::cout << "exported " << files.size() << " stems" << std::endl;
            juce::MemoryBlock mb;
            proc.getStateInformation (mb);
            std::cout << "state " << mb.getSize() << " bytes, tracks " << s.doc.tracks.size() << std::endl;
            finish();
            break;
        }
        default: break;
    }
}
