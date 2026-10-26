// Offline checks for the HARMIVOX-6 engine.
// Builds a synthetic "singer" (pulse train through vowel formants, with vibrato
// and slightly flat notes), then checks detection, tuning, harmonies and timing.

#include "engine/Model.h"
#include "engine/Analysis.h"
#include "engine/Renderer.h"
#include "engine/Harmony.h"
#include "engine/Aligner.h"
#include "engine/Finish.h"
#include "engine/Cleaner.h"
#include "engine/VoiceProfile.h"
#include "engine/Session.h"
#include <iostream>

using namespace hvx;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::cout << "  ok   " << msg << "\n"; else { std::cout << "  FAIL " << msg << "\n"; ++failures; } } while (0)

struct SungNote { double start, len; float midi; };

static juce::AudioBuffer<float> sing (const std::vector<SungNote>& notes, double sr, double total, float vibCents = 35.0f, juce::uint32 seed = 1, double f1 = 730)
{
    juce::AudioBuffer<float> b (1, (int) (total * sr));
    b.clear();
    float* x = b.getWritePointer (0);
    juce::Random rng ((juce::int64) seed);

    // formant resonators (vowel "ah")
    struct Res { double f, bw, g; double y1 = 0, y2 = 0; };
    for (auto& n : notes)
    {
        Res res[] = { { f1, 90, 1.0 }, { 1090, 110, 0.5 }, { 2440, 160, 0.25 }, { 3400, 250, 0.1 } };
        double phase = 0;
        const int s0 = (int) (n.start * sr), s1 = (int) ((n.start + n.len) * sr);
        for (int i = s0; i < s1 && i < b.getNumSamples(); ++i)
        {
            const double t = (i - s0) / sr;
            const double vib = vibCents / 100.0 * std::sin (2 * juce::MathConstants<double>::pi * 5.6 * t) * juce::jmin (1.0, t / 0.3);
            const double f0 = midiToHz (n.midi + vib);
            phase += f0 / sr;
            double pulse = 0;
            if (phase >= 1.0) { phase -= 1.0; pulse = 1.0; }
            double src = pulse + 0.01 * (rng.nextDouble() - 0.5);
            double out = 0;
            for (auto& r : res)
            {
                const double R = std::exp (-juce::MathConstants<double>::pi * r.bw / sr);
                const double c = 2 * R * std::cos (2 * juce::MathConstants<double>::pi * r.f / sr);
                const double y = src * (1 - R) * r.g * 8 + c * r.y1 - R * R * r.y2;
                r.y2 = r.y1; r.y1 = y; out += y;
            }
            const double env = juce::jmin (1.0, t / 0.02) * juce::jmin (1.0, (n.len - t) / 0.03);
            x[i] += (float) (out * env * 0.3);
        }
    }
    return b;
}

static float measuredPitch (const juce::AudioBuffer<float>& audio, double sr, double from, double to)
{
    juce::AudioBuffer<float> m (1, audio.getNumSamples());
    m.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
    auto src = Analysis::analyse (m, sr, "measure");
    std::vector<float> ps;
    for (double t = from; t < to; t += 0.005) { float p; if (src->pitchAt (t, p)) ps.push_back (p); }
    return ps.empty() ? 0.0f : Analysis::medianOf (ps);
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    // C major phrase, sung a little flat/sharp: E3 G3 A3 G3 C4
    std::vector<SungNote> melody { { 0.20, 0.55, 51.7f }, { 0.78, 0.50, 55.2f }, { 1.30, 0.45, 56.8f },
                                   { 1.78, 0.40, 55.1f }, { 2.22, 0.90, 59.7f } };
    auto audio = sing (melody, sr, 3.4);
    writeWav (outDir.getChildFile ("01_input.wav"), audio, sr);

    std::cout << "Analysis\n";
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto src = Analysis::analyse (audio, sr, "Take 1");
    std::cout << "  (analysis took " << juce::Time::getMillisecondCounterHiRes() - t0 << " ms, marks " << src->marks.size() << ")\n";
    int nid = 1;
    auto notes = Analysis::detectNotes (*src, nid);
    CHECK (notes.size() == melody.size(), "found " + std::to_string (notes.size()) + " notes (expected 5)");
    for (size_t i = 0; i < juce::jmin (notes.size(), melody.size()); ++i)
    {
        CHECK (std::abs (notes[i].detected - melody[i].midi) < 0.25f,
               "note " + std::to_string (i + 1) + " detected " + juce::String (notes[i].detected, 2).toStdString()
               + " sung " + juce::String (melody[i].midi, 2).toStdString());
        CHECK (std::abs (notes[i].srcStart - melody[i].start) < 0.06, "note " + std::to_string (i + 1) + " start "
               + juce::String (notes[i].srcStart, 3).toStdString());
    }
    {
        // strong 2nd harmonic (vowel formant right on it) must not flip octaves
        std::vector<SungNote> hi { { 0.1, 1.0, 64.3f }, { 1.2, 0.8, 62.0f }, { 2.1, 0.9, 66.9f } };
        bool okAll = true;
        for (double f1 : { 650.0, 700.0, 780.0, 900.0 })
        {
            auto a = sing (hi, sr, 3.2, 35.0f, 3, f1);
            auto s2 = Analysis::analyse (a, sr, "hi");
            int n2 = 1;
            auto ns = Analysis::detectNotes (*s2, n2);
            bool ok = ns.size() == 3;
            for (size_t i = 0; ok && i < 3; ++i) ok = std::abs (ns[i].detected - hi[i].midi) < 0.3f;
            if (! ok) { okAll = false; std::cout << "    F1 " << f1 << ": " << ns.size() << " notes"; for (auto& n : ns) std::cout << " " << n.detected; std::cout << "\n"; }
        }
        CHECK (okAll, "no octave flips when a formant sits on the 2nd harmonic");
    }
    int key = -1, scale = -1;
    Analysis::detectKey (notes, key, scale);
    std::cout << "  key " << keyNames()[key] << " " << scaleNames()[scale] << "\n";

    Document doc;
    doc.key = 0; doc.scale = 0; doc.bpm = 120;
    Track lead;
    lead.id = 1; lead.name = "Take 1"; lead.isLead = true; lead.source = src; lead.notes = notes;
    for (auto& n : lead.notes) n.target = (float) snapToScale (n.detected, 0, 0);
    lead.tune.strength = 1.0f; lead.tune.retuneMs = 30; lead.tune.vibrato = 1.0f;
    doc.tracks.push_back (lead);

    std::cout << "Tuning (lead)\n";
    {
        Track t = lead; t.finish.enabled = false;
        Document d = doc; d.finishAllTracks = false;
        t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = Session::renderTrack (t, d, nullptr, sr);
        std::cout << "  (render took " << juce::Time::getMillisecondCounterHiRes() - t0 << " ms)\n";
        writeWav (outDir.getChildFile ("02_lead_tuned.wav"), r->stereo, sr);
        const float expected[] = { 52, 55, 57, 55, 60 };
        for (size_t i = 0; i < melody.size(); ++i)
        {
            const float p = measuredPitch (r->stereo, sr, melody[i].start + 0.12, melody[i].start + melody[i].len - 0.08);
            CHECK (std::abs (p - expected[i]) < 0.15f, "tuned note " + std::to_string (i + 1) + " = "
                   + juce::String (p, 2).toStdString() + " (target " + std::to_string ((int) expected[i]) + ")");
        }
        // level sanity: output RMS close to input RMS
        const float rin = audio.getRMSLevel (0, 0, audio.getNumSamples());
        const float rout = r->stereo.getRMSLevel (0, 0, juce::jmin (audio.getNumSamples(), r->stereo.getNumSamples()));
        CHECK (rout > rin * 0.6f && rout < rin * 1.6f, "level kept (in " + juce::String (rin, 4).toStdString() + " out " + juce::String (rout, 4).toStdString() + ")");
    }

    std::cout << "Harmony 3rd above (follow key, C major)\n";
    {
        VoiceSpec v; v.parentId = 1; v.steps = 2; v.mode = IntervalMode::FollowKey;
        const float exp3[] = { 55, 59, 60, 59, 64 };   // E->G, G->B, A->C, G->B, C->E
        Track voice = lead;
        voice.id = 2; voice.kind = TrackKind::Voice; voice.isLead = false; voice.voice = v;
        voice.human.seed = 1234; voice.human.variation = 0.2f; voice.human.looseMs = 12;
        for (auto& n : voice.notes) n.target = Harmony::harmonyTarget (n.target, v, 0, 0, {}, 0);
        for (size_t i = 0; i < voice.notes.size(); ++i)
            CHECK (std::abs (voice.notes[i].target - exp3[i]) < 0.01f, "harmony target " + std::to_string (i + 1) + " = " + juce::String (voice.notes[i].target).toStdString());
        voice.finish.enabled = false;
        Document d = doc; d.finishAllTracks = false; d.stereo.stereoize = false;
        auto r = Session::renderTrack (voice, d, nullptr, sr);
        writeWav (outDir.getChildFile ("03_third_above.wav"), r->stereo, sr);
        for (size_t i = 0; i < melody.size(); ++i)
        {
            const float p = measuredPitch (r->stereo, sr, melody[i].start + 0.15, melody[i].start + melody[i].len - 0.1);
            CHECK (std::abs (p - exp3[i]) < 0.3f, "voice note " + std::to_string (i + 1) + " = " + juce::String (p, 2).toStdString());
        }
        // stack them
        juce::AudioBuffer<float> mix (2, r->stereo.getNumSamples());
        mix.clear();
        auto rl = Session::renderTrack ([&] { Track t = lead; t.finish.enabled = false; return t; }(), d, nullptr, sr);
        for (int c = 0; c < 2; ++c)
        {
            mix.addFrom (c, 0, rl->stereo, c, 0, juce::jmin (mix.getNumSamples(), rl->stereo.getNumSamples()), 0.7f);
            mix.addFrom (c, 0, r->stereo, c, 0, mix.getNumSamples(), 0.5f);
        }
        writeWav (outDir.getChildFile ("04_lead_plus_third.wav"), mix, sr);
    }

    std::cout << "Follow chords + auto chords\n";
    {
        doc.tracks[0].clipStart = 0;
        auto chords = Harmony::autoChords (doc);
        juce::String cs; for (auto& c : chords) cs << chordName (c) << " ";
        std::cout << "  chords: " << cs << "\n";
        CHECK (! chords.empty(), "auto chords produced");
        VoiceSpec v; v.steps = 2; v.mode = IntervalMode::FollowChords;
        Chord cmaj { 0, 10, 0, 0 };
        const float t = Harmony::harmonyTarget (52.0f, v, 0, 0, { cmaj }, 1.0);   // E3 over C -> G3
        CHECK (std::abs (t - 55.0f) < 0.01f, "E3 over C major, chord 3rd above = G3 (" + juce::String (t).toStdString() + ")");
        v.steps = -2;
        const float tb = Harmony::harmonyTarget (52.0f, v, 0, 0, { cmaj }, 1.0);  // below -> C3
        CHECK (std::abs (tb - 48.0f) < 0.01f, "E3 over C major, chord 3rd below = C3 (" + juce::String (tb).toStdString() + ")");
        v.steps = -7; v.mode = IntervalMode::FollowKey;
        CHECK (std::abs (Harmony::harmonyTarget (52.0f, v, 0, 0, {}, 0) - 40.0f) < 0.01f, "octave below = -12");
        v.steps = 4;
        CHECK (std::abs (Harmony::harmonyTarget (57.0f, v, 0, 0, {}, 0) - 64.0f) < 0.01f, "A3 5th above in C = E4");
    }

    std::cout << "Quantize + warps\n";
    {
        Track t = lead;
        Aligner::quantize (t, 120.0, 16, 1.0f);   // 1/16 at 120 = 125 ms grid
        bool allOn = true;
        for (auto& n : t.notes)
        {
            const double d = srcToDst (t.warps, n.srcStart);
            const double off = std::abs (d - std::round (d / 0.125) * 0.125);
            if (off > 0.003) allOn = false;
        }
        CHECK (allOn, "note starts land on the 1/16 grid");
        auto plan = buildPlan (t, *src);
        CHECK (plan.size() > 10, "plan follows warps");
        auto r = Session::renderTrack (t, doc, nullptr, sr);
        writeWav (outDir.getChildFile ("05_quantized.wav"), r->stereo, sr);
    }

    std::cout << "Align a late double to the lead (DTW)\n";
    {
        // second take: same melody, notes 40-70 ms late
        std::vector<SungNote> late = melody;
        for (size_t i = 0; i < late.size(); ++i) late[i].start += 0.04 + 0.01 * (double) i;
        auto a2 = sing (late, sr, 3.6, 30.0f, 7);
        auto s2 = Analysis::analyse (a2, sr, "Take 2");
        Track take; take.id = 3; take.source = s2; take.notes = Analysis::detectNotes (*s2, nid);
        Aligner::alignTakeToLead (take, doc.tracks[0], 1.0f, true);
        double worst = 0;
        for (size_t i = 0; i < juce::jmin (take.notes.size(), notes.size()); ++i)
        {
            const double d = srcToDst (take.warps, take.notes[i].srcStart);
            worst = juce::jmax (worst, std::abs (d - notes[i].srcStart));
        }
        CHECK (worst < 0.03, "aligned note starts within 30 ms of the lead (worst " + juce::String (worst * 1000, 1).toStdString() + " ms)");
    }

    std::cout << "Clean + Finish run\n";
    {
        juce::AudioBuffer<float> noisy (audio);
        juce::Random rng (3);
        for (int i = 0; i < noisy.getNumSamples(); ++i) noisy.getWritePointer (0)[i] += 0.01f * (rng.nextFloat() - 0.5f);
        auto ns = Analysis::analyse (noisy, sr, "noisy");
        CleanSettings cs; cs.noiseReduce = 0.8f; cs.gate = 0.5f;
        auto cleaned = Cleaner::process (*ns, notes, cs);
        const float noiseBefore = noisy.getRMSLevel (0, 0, (int) (0.15 * sr));
        const float noiseAfter = cleaned.getRMSLevel (0, 0, (int) (0.15 * sr));
        CHECK (noiseAfter < noiseBefore * 0.3f, "noise in the gap dropped (" + juce::String (juce::Decibels::gainToDecibels (noiseAfter / noiseBefore), 1).toStdString() + " dB)");
        juce::AudioBuffer<float> f (audio);
        Finish::process (f, sr, Finish::preset (2), 0.0f);
        CHECK (f.getMagnitude (0, 0, f.getNumSamples()) < 1.5f && f.getRMSLevel (0, 0, f.getNumSamples()) > 0.001f, "finish chain output sane");
        writeWav (outDir.getChildFile ("06_finished.wav"), f, sr);
    }

    std::cout << "Voice profile\n";
    {
        std::vector<VoiceProfile::Take> takes;
        for (int k = 0; k < 4; ++k)
        {
            std::vector<SungNote> sc;
            for (int s = 0; s < 6; ++s) sc.push_back ({ 0.1 + s * 0.6, 0.55, 45.0f + k * 4 + s * 2.0f });
            auto a = sing (sc, sr, 4.0, 30.0f, (juce::uint32) (11 + k));
            takes.push_back ({ Analysis::analyse (a, sr, "lab"), "scales", 0.0 });
        }
        auto p = VoiceProfile::build (takes, "Test voice", "Middle");
        CHECK (p.valid, "profile built from " + std::to_string (p.takesUsed) + " takes, range " + noteName (p.lowMidi).toStdString() + "-" + noteName (p.highMidi).toStdString());
        CHECK (p.vibratoRateHz > 4.5f && p.vibratoRateHz < 6.8f, "vibrato rate " + juce::String (p.vibratoRateHz, 2).toStdString() + " Hz (sung 5.6)");
        auto f = juce::File::createTempFile (".hvxvoice");
        p.save (f);
        auto q = VoiceProfile::load (f);
        CHECK (q.valid && std::abs (q.env[5][10] - p.env[5][10]) < 0.02f, "profile saves and loads");

        Track voice = doc.tracks[0];
        voice.kind = TrackKind::Voice; voice.voice.steps = 4; voice.useProfile = true; voice.finish.enabled = false;
        for (auto& n : voice.notes) n.target = Harmony::harmonyTarget (n.target, voice.voice, 0, 0, {}, 0);
        Document d = doc; d.finishAllTracks = false;
        auto r = Session::renderTrack (voice, d, &p, sr);
        CHECK (r->stereo.getRMSLevel (0, 0, r->stereo.getNumSamples()) > 0.001f, "voice renders with profile shaping");
        writeWav (outDir.getChildFile ("07_fifth_with_profile.wav"), r->stereo, sr);
    }

    std::cout << "Resample to 44.1k playback\n";
    {
        auto r = Session::renderTrack (doc.tracks[0], doc, nullptr, 44100.0);
        const double expLen = (r->stereo.getNumSamples() / 44100.0);
        CHECK (expLen > 3.3 && expLen < 4.0, "length at 44.1k = " + juce::String (expLen, 2).toStdString() + " s");
        const float p = measuredPitch (r->stereo, 44100.0, 2.4, 3.0);
        CHECK (std::abs (p - 60.0f) < 0.2f, "pitch kept after resampling (" + juce::String (p, 2).toStdString() + ")");
    }

    std::cout << "\n" << (failures == 0 ? "ALL PASSED" : std::to_string (failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
