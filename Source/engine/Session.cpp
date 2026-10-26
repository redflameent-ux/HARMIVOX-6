#include "Session.h"
#include "Analysis.h"
#include "Cleaner.h"
#include "Aligner.h"
#include "Finish.h"

namespace hvx
{
//==============================================================================
namespace
{
struct Hasher
{
    juce::uint64 h = 1469598103934665603ull;
    void bytes (const void* p, size_t n)
    {
        auto* b = static_cast<const juce::uint8*> (p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    }
    template <typename T> void add (const T& v) { bytes (&v, sizeof (T)); }
    void str (const juce::String& s) { bytes (s.toRawUTF8(), s.getNumBytesAsUTF8()); }
};

void callOnMessageThread (std::shared_ptr<std::atomic<bool>> alive, std::function<void()> fn)
{
    juce::MessageManager::callAsync ([alive, fn = std::move (fn)]
    {
        if (alive->load()) fn();
    });
}

juce::AudioBuffer<float> toMono (const juce::AudioBuffer<float>& in)
{
    juce::AudioBuffer<float> m (1, in.getNumSamples());
    m.clear();
    const int ch = in.getNumChannels();
    for (int c = 0; c < ch; ++c) m.addFrom (0, 0, in, c, 0, in.getNumSamples(), 1.0f / (float) ch);
    return m;
}

juce::AudioBuffer<float> resampleTo (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
{
    if (std::abs (fromRate - toRate) < 0.5) return in;
    const double ratio = fromRate / toRate;
    const int outN = (int) std::ceil (in.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (in.getNumChannels(), outN);
    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, in.getReadPointer (c), out.getWritePointer (c), outN, in.getNumSamples(), 0);
    }
    return out;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& buf, double rate, int bits)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned int) buf.getNumChannels(), bits, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}
} // namespace

//==============================================================================
Session::Session()
{
    sessionId = juce::Uuid().toString().substring (0, 8);
    startTimerHz (8);
}

Session::~Session()
{
    alive->store (false);
    stopTimer();
    pool.removeAllJobs (true, 4000);
}

juce::File Session::folder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("HARMIVOX-6").getChildFile ("Sessions").getChildFile (sessionId);
    f.createDirectory();
    return f;
}

//==============================================================================
void Session::checkpoint (const juce::String& label)
{
    undoStack.push_back ({ label, doc });
    if (undoStack.size() > 120) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void Session::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back ({ undoStack.back().first, doc });
    doc = undoStack.back().second;
    undoStack.pop_back();
    changed();
}

void Session::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back ({ redoStack.back().first, doc });
    doc = redoStack.back().second;
    redoStack.pop_back();
    changed();
}

juce::StringArray Session::historyLabels() const
{
    juce::StringArray s;
    s.add ("Start");
    for (auto& u : undoStack) s.add (u.first);
    for (auto it = redoStack.rbegin(); it != redoStack.rend(); ++it) s.add (it->first);
    return s;
}

void Session::jumpToHistory (int index)
{
    while ((int) undoStack.size() > index && ! undoStack.empty())
    {
        redoStack.push_back ({ undoStack.back().first, doc });
        doc = undoStack.back().second;
        undoStack.pop_back();
    }
    while ((int) undoStack.size() < index && ! redoStack.empty())
    {
        undoStack.push_back ({ redoStack.back().first, doc });
        doc = redoStack.back().second;
        redoStack.pop_back();
    }
    changed();
}

//==============================================================================
void Session::changed()
{
    // keep linked voices in step with their parent
    for (auto& t : doc.tracks)
        if (t.isVoice() && t.voice.followParent) regenerateVoice (t);

    if (doc.autoChords)
        doc.chords = Harmony::autoChords (doc);
    // chords may have moved: harmonies that follow chords need another pass
    for (auto& t : doc.tracks)
        if (t.isVoice() && t.voice.followParent && t.voice.mode == IntervalMode::FollowChords) regenerateVoice (t);

    scheduleRenders();
    publishPlayback();
    sendChangeMessage();
}

//==============================================================================
void Session::importAudio (const juce::AudioBuffer<float>& audio, double sr, const juce::String& name, double clipStart)
{
    if (audio.getNumSamples() < (int) (0.2 * sr)) return;
    auto mono = std::make_shared<juce::AudioBuffer<float>> (toMono (audio));
    const int tid = doc.nextTrackId++;
    const auto fileName = "take_" + juce::String (tid) + "_" + juce::String (juce::Time::currentTimeMillis() % 100000) + ".wav";
    const auto file = folder().getChildFile (fileName);
    ++pendingImportCount;
    auto aliveFlag = alive;

    pool.addJob ([this, aliveFlag, mono, sr, name, clipStart, tid, file, fileName]
    {
        writeWav (file, *mono, sr, 32);
        auto src = Analysis::analyse (*mono, sr, name);
        if (src != nullptr) src->fileName = fileName;
        callOnMessageThread (aliveFlag, [this, src, name, clipStart, tid]
        {
            --pendingImportCount;
            if (src != nullptr) finishImport (src, name, clipStart, tid);
            else sendChangeMessage();
        });
    });
    sendChangeMessage();
}

bool Session::importFile (const juce::File& file, double clipStart)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return false;
    const auto maxLen = (juce::int64) (r->sampleRate * 60 * 12);
    const int len = (int) juce::jmin (r->lengthInSamples, maxLen);
    juce::AudioBuffer<float> buf ((int) r->numChannels, len);
    r->read (&buf, 0, len, 0, true, true);
    importAudio (buf, r->sampleRate, file.getFileNameWithoutExtension(), clipStart);
    return true;
}

void Session::finishImport (std::shared_ptr<SourceAudio> src, const juce::String& name, double clipStart, int tid)
{
    checkpoint ("Import " + name);
    Track t;
    t.id = tid;
    int takes = 0;
    for (auto& tr : doc.tracks) if (! tr.isVoice()) ++takes;
    t.name = name.isNotEmpty() ? name : "Take " + juce::String (takes + 1);
    t.colour = trackColourForIndex ((int) doc.tracks.size());
    t.kind = TrackKind::Take;
    t.source = src;
    t.clipStart = clipStart;
    t.notes = Analysis::detectNotes (*src, doc.nextNoteId);
    t.isLead = doc.lead() == nullptr;

    if (t.isLead && doc.autoKey && ! t.notes.empty())
        Analysis::detectKey (t.notes, doc.key, doc.scale);
    retargetToKey (t);

    doc.tracks.push_back (std::move (t));
    changed();
}

//==============================================================================
void Session::retargetToKey (Track& t) const
{
    for (auto& n : t.notes)
        if (! n.edited)
            n.target = doc.scale == 9 ? (float) std::lround (n.detected) : (float) snapToScale (n.detected, doc.key, doc.scale);
}

void Session::setKey (bool autoKey, int key, int scale)
{
    doc.autoKey = autoKey;
    if (autoKey)
    {
        if (auto* l = doc.lead()) Analysis::detectKey (l->notes, doc.key, doc.scale);
    }
    else { doc.key = key; doc.scale = scale; }
    for (auto& t : doc.tracks) if (! t.isVoice()) retargetToKey (t);
    changed();
}

//==============================================================================
void Session::regenerateVoice (Track& v) const
{
    const Track* p = doc.find (v.voice.parentId);
    if (p == nullptr || p->isVoice()) return;
    v.source = p->source;
    v.clipStart = p->clipStart;
    v.warps = p->warps;
    v.tune = p->tune;
    v.notes = p->notes;
    for (auto& n : v.notes)
    {
        const double host = p->clipStart + srcToDst (p->warps, 0.5 * (n.srcStart + n.srcEnd));
        n.target = Harmony::harmonyTarget (n.target, v.voice, doc.key, doc.scale, doc.chords, host);
    }
}

std::vector<int> Session::addVoices (const AddVoicesOptions& o)
{
    std::vector<int> ids;
    const Track* parent = doc.find (o.parentId);
    if (parent == nullptr) parent = doc.lead();
    if (parent == nullptr) return ids;
    if (parent->isVoice()) parent = doc.find (parent->voice.parentId);
    if (parent == nullptr) return ids;

    const int parentId = parent->id;
    const juce::String parentName = parent->name;
    juce::Random rng;

    // insert after the parent's existing voices
    int insertAt = doc.indexOf (parentId) + 1;
    while (insertAt < (int) doc.tracks.size() && doc.tracks[(size_t) insertAt].isVoice()
           && doc.tracks[(size_t) insertAt].voice.parentId == parentId) ++insertAt;

    const float profLoose = (voiceProfile != nullptr && voiceProfile->valid && o.useProfile) ? voiceProfile->looseMs : 12.0f;

    int singleIdx = 0;
    for (auto& req : o.voices)
    {
        for (int k = 0; k < juce::jmax (1, req.takes); ++k)
        {
            Track v;
            v.id = doc.nextTrackId++;
            v.kind = TrackKind::Voice;
            v.voice.parentId = parentId;
            v.voice.steps = req.steps;
            v.voice.semitones = Harmony::exactSemitones (req.steps);
            v.voice.mode = o.mode;
            v.voice.followParent = true;
            v.name = Harmony::voiceName (req.steps, k, req.takes, parentName);
            v.colour = trackColourForIndex ((int) doc.tracks.size() + (int) ids.size());
            v.gainDb = req.levelDb + o.levelDb;
            v.human.seed = (juce::uint32) rng.nextInt() | 1u;
            v.human.looseMs = profLoose * (0.35f + 1.6f * o.looseness);
            v.human.variation = 0.15f + 0.6f * o.looseness;
            v.human.otherSinger = o.otherSingers;
            v.useProfile = o.useProfile;

            float pan = 0.0f;
            if (req.takes == 2) pan = (k == 0 ? -0.9f : 0.9f);
            else if (req.takes >= 3) { static const float q[] = { -0.55f, 0.55f, -1.0f, 1.0f, -0.3f, 0.3f }; pan = q[k % 6]; }
            else pan = (singleIdx++ % 2 == 0 ? -0.35f : 0.35f);
            v.pan = pan * o.panSpread;

            regenerateVoice (v);
            ids.push_back (v.id);
            doc.tracks.insert (doc.tracks.begin() + insertAt++, std::move (v));
        }
    }
    changed();
    return ids;
}

void Session::reroll (int id)
{
    if (auto* t = doc.find (id))
    {
        checkpoint ("Reroll " + t->name);
        t->human.seed = (juce::uint32) juce::Random().nextInt() | 1u;
        changed();
    }
}

void Session::resyncVoice (int id)
{
    if (auto* t = doc.find (id); t != nullptr && t->isVoice())
    {
        checkpoint ("Re-sync " + t->name);
        t->voice.followParent = true;
        changed();
    }
}

void Session::deleteTrack (int id)
{
    const int idx = doc.indexOf (id);
    if (idx < 0) return;
    checkpoint ("Delete " + doc.tracks[(size_t) idx].name);
    const bool wasLead = doc.tracks[(size_t) idx].isLead;
    for (auto& t : doc.tracks) if (t.isVoice() && t.voice.parentId == id) t.voice.followParent = false;
    doc.tracks.erase (doc.tracks.begin() + idx);
    if (wasLead)
        for (auto& t : doc.tracks) if (! t.isVoice()) { t.isLead = true; break; }
    changed();
}

int Session::duplicateTrack (int id)
{
    const int idx = doc.indexOf (id);
    if (idx < 0) return -1;
    checkpoint ("Duplicate " + doc.tracks[(size_t) idx].name);
    Track c = doc.tracks[(size_t) idx];
    c.id = doc.nextTrackId++;
    c.isLead = false;
    c.name = c.name + " copy";
    c.human.seed = (juce::uint32) juce::Random().nextInt() | 1u;
    c.colour = trackColourForIndex ((int) doc.tracks.size());
    if (! c.isVoice())
    {
        // a copied take becomes a free double of itself
        c.kind = TrackKind::Voice;
        c.voice.parentId = id;
        c.voice.steps = 0;
        c.voice.followParent = true;
    }
    doc.tracks.insert (doc.tracks.begin() + idx + 1, c);
    changed();
    return c.id;
}

void Session::setLead (int id)
{
    auto* t = doc.find (id);
    if (t == nullptr || t->isVoice()) return;
    checkpoint ("Set lead " + t->name);
    for (auto& x : doc.tracks) x.isLead = (x.id == id);
    changed();
}

//==============================================================================
void Session::quantize (int id, int division, float amount)
{
    checkpoint (id < 0 ? "Quantize all" : "Quantize track");
    for (auto& t : doc.tracks)
    {
        if (id >= 0 && t.id != id) continue;
        if (id < 0 && t.isVoice() && t.voice.followParent) continue;  // they follow their parent
        if (t.isVoice()) t.voice.followParent = false;
        Aligner::quantize (t, doc.bpm, division, amount);
    }
    changed();
}

void Session::alignToLead (int id, float tightness, bool ends)
{
    const Track* lead = doc.lead();
    if (lead == nullptr) return;
    checkpoint (id < 0 ? "Align all to lead" : "Align to lead");
    const Track leadCopy = *lead;
    for (auto& t : doc.tracks)
    {
        if (t.isLead || (id >= 0 && t.id != id)) continue;
        if (t.isVoice())
        {
            t.human.timingScale = 1.0f - tightness;
            if (! t.voice.followParent)
            {
                if (t.source == leadCopy.source) { t.warps = leadCopy.warps; t.clipStart = leadCopy.clipStart; }
                else Aligner::alignTakeToLead (t, leadCopy, tightness, ends);
            }
        }
        else Aligner::alignTakeToLead (t, leadCopy, tightness, ends);
    }
    changed();
}

void Session::clearWarps (int id)
{
    checkpoint ("Clear warp markers");
    for (auto& t : doc.tracks)
        if (id < 0 || t.id == id) { t.warps.clear(); t.human.timingScale = 1.0f; }
    changed();
}

//==============================================================================
void Session::setPlaybackRate (double sr)
{
    if (sr <= 0 || std::abs (sr - playRate.load()) < 0.5) return;
    playRate.store (sr);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
    {
        auto a = alive;
        juce::MessageManager::callAsync ([this, a] { if (a->load()) changed(); });
    }
}

void Session::setProfile (std::shared_ptr<const VoiceProfile> p)
{
    voiceProfile = std::move (p);
    changed();
}

ResultPtr Session::result (int id) const
{
    std::lock_guard<std::mutex> l (resultMutex);
    auto it = results.find (id);
    return it != results.end() ? it->second : nullptr;
}

juce::String Session::statusOf (int id) const
{
    std::lock_guard<std::mutex> l (resultMutex);
    auto it = status.find (id);
    return it != status.end() ? it->second : juce::String();
}

bool Session::isBusy (int id) const
{
    const auto s = statusOf (id);
    return s.startsWith ("Rendering") || s.startsWith ("Queued");
}

int Session::busyCount() const
{
    std::lock_guard<std::mutex> l (resultMutex);
    int c = 0;
    for (auto& [id, s] : status) if (s.startsWith ("Rendering") || s.startsWith ("Queued")) ++c;
    return c + pendingImportCount.load();
}

float Session::progressOf (int id) const
{
    return isBusy (id) ? 0.5f : 1.0f;
}

std::shared_ptr<const Plan> Session::planFor (int id)
{
    const Track* t = doc.find (id);
    if (t == nullptr || t->source == nullptr) return nullptr;
    Hasher h;
    for (auto& n : t->notes) { h.add (n.id); h.add (n.srcStart); h.add (n.srcEnd); h.add (n.target); h.add (n.detected); h.add (n.volumeDb); h.add (n.formantSt); h.add (n.driftFix); h.add (n.vibrato); h.add (n.addVibratoCents); h.add (n.vibratoRateHz); h.add (n.transition); h.add (n.muted); }
    for (auto& w : t->warps) { h.add (w.src); h.add (w.dst); }
    h.add (t->tune.strength); h.add (t->tune.retuneMs); h.add (t->tune.driftFix); h.add (t->tune.vibrato);
    h.add (t->human.seed); h.add (t->human.variation); h.add (t->human.looseMs); h.add (t->human.timingScale);
    h.add (t->human.otherSinger); h.add (t->human.belt);
    h.add ((juce::pointer_sized_int) t->source.get());
    auto it = planCache.find (id);
    if (it != planCache.end() && it->second.first == h.h) return it->second.second;
    auto p = std::make_shared<const Plan> (buildPlan (*t, *t->source));
    planCache[id] = { h.h, p };
    return p;
}

//==============================================================================
FinishSettings Session::resolvedFinish (const Track& t, const Document& d)
{
    return d.finishAllTracks ? d.finish : t.finish;
}

CleanSettings Session::resolvedClean (const Track& t, const Document& d)
{
    if (t.isVoice())
        if (auto* p = d.find (t.voice.parentId)) return p->clean;
    return t.clean;
}

juce::uint64 Session::renderSignature (const Track& t, const Document& d, const juce::String& profileKey, double rate)
{
    Hasher h;
    h.add (t.id);
    h.add ((juce::pointer_sized_int) t.source.get());
    h.add (t.clipStart);
    h.add (t.kind);
    for (auto& n : t.notes)
    {
        h.add (n.srcStart); h.add (n.srcEnd); h.add (n.target); h.add (n.detected); h.add (n.volumeDb);
        h.add (n.formantSt); h.add (n.driftFix); h.add (n.vibrato); h.add (n.addVibratoCents);
        h.add (n.vibratoRateHz); h.add (n.transition); h.add (n.muted); h.add (n.id);
    }
    for (auto& w : t.warps) { h.add (w.src); h.add (w.dst); }
    h.add (t.tune.strength); h.add (t.tune.retuneMs); h.add (t.tune.driftFix); h.add (t.tune.vibrato);
    const auto c = resolvedClean (t, d);
    h.add (c.noiseReduce); h.add (c.breathDb); h.add (c.gate); h.add (c.deClick);
    h.add (t.human.seed); h.add (t.human.variation); h.add (t.human.otherSinger); h.add (t.human.belt);
    h.add (t.human.looseMs); h.add (t.human.timingScale);
    const auto f = resolvedFinish (t, d);
    h.add (f.enabled); h.add (f.lowCutHz); h.add (f.deplosive); h.add (f.mudCutDb); h.add (f.deEsser);
    h.add (f.presenceDb); h.add (f.airDb); h.add (f.compressDb); h.add (f.ratio);
    h.add (t.isVoice() ? d.stereo.stereoize : false); h.add (d.stereo.stereoizeAmt);
    h.add (t.useProfile);
    h.str (t.isVoice() && t.useProfile ? profileKey : juce::String());
    h.add (rate);
    return h.h;
}

std::shared_ptr<const juce::AudioBuffer<float>> Session::cleanedFor (const Track& t, const Document& d)
{
    const auto cs = resolvedClean (t, d);
    std::lock_guard<std::mutex> l (cleanMutex);
    for (auto& e : cleanCache)
        if (e.src == t.source.get() && e.s == cs) return e.buf;
    return nullptr;
}

std::shared_ptr<RenderResult> Session::renderTrack (const Track& t, const Document& d, const VoiceProfile* profile,
                                                    double rate, const juce::AudioBuffer<float>* cleaned)
{
    auto r = std::make_shared<RenderResult>();
    r->trackId = t.id;
    r->rate = rate;
    r->startSec = t.clipStart;
    if (t.source == nullptr) return r;
    const auto& src = *t.source;

    juce::AudioBuffer<float> cleanLocal;
    if (cleaned == nullptr)
    {
        cleanLocal = Cleaner::process (src, t.notes, resolvedClean (t, d));
        cleaned = &cleanLocal;
    }

    const Plan plan = buildPlan (t, src);
    auto mono = renderPsola (src, *cleaned, plan);

    if (t.isVoice() && t.useProfile && profile != nullptr && profile->valid)
        VoiceProfile::apply (mono, src.sampleRate, plan, *profile, 0.85f);

    Finish::process (mono, src.sampleRate, resolvedFinish (t, d), t.isVoice() ? t.human.belt : 0.0f);

    const float st = (t.isVoice() && d.stereo.stereoize) ? d.stereo.stereoizeAmt : 0.0f;
    auto stereo = Finish::toStereo (mono, src.sampleRate, st, t.human.seed);
    r->stereo = resampleTo (stereo, src.sampleRate, rate);
    return r;
}

void Session::scheduleRenders()
{
    const double rate = playRate.load();
    const juce::String profileKey = voiceProfile != nullptr ? voiceProfile->name + juce::String (voiceProfile->takesUsed)
                                                              + juce::String (voiceProfile->vibratoRateHz) : juce::String();

    // forget tracks that no longer exist
    {
        std::lock_guard<std::mutex> l (resultMutex);
        for (auto it = results.begin(); it != results.end();)
            if (doc.find (it->first) == nullptr) it = results.erase (it); else ++it;
        for (auto it = status.begin(); it != status.end();)
            if (doc.find (it->first) == nullptr) it = status.erase (it); else ++it;
    }
    {
        std::lock_guard<std::mutex> l (resultMutex);
        for (auto it = scheduledSig.begin(); it != scheduledSig.end();)
            if (doc.find (it->first) == nullptr) it = scheduledSig.erase (it); else ++it;
    }

    Document ctx;
    ctx.stereo = doc.stereo;
    ctx.finish = doc.finish;
    ctx.finishAllTracks = doc.finishAllTracks;

    for (auto& t : doc.tracks)
    {
        if (t.source == nullptr) continue;
        const auto sig = renderSignature (t, doc, profileKey, rate);
        {
            std::lock_guard<std::mutex> l (resultMutex);
            if (scheduledSig[t.id] == sig) continue;
            scheduledSig[t.id] = sig;
            status[t.id] = "Rendering";
        }

        Track snap = t;
        snap.clean = resolvedClean (t, doc);
        snap.finish = resolvedFinish (t, doc);
        Document local = ctx;
        local.finishAllTracks = false;   // snap.finish already resolved
        auto prof = voiceProfile;
        auto aliveFlag = alive;
        const int id = t.id;

        pool.addJob ([this, aliveFlag, snap, local, prof, rate, sig, id]
        {
            if (! aliveFlag->load()) return;
            // drop the job if a newer edit has already been scheduled
            {
                std::lock_guard<std::mutex> l (resultMutex);
                if (scheduledSig[id] != sig) return;
            }
            std::shared_ptr<const juce::AudioBuffer<float>> cleaned;
            if (! Cleaner::isNeutral (snap.clean))
            {
                {
                    std::lock_guard<std::mutex> l (cleanMutex);
                    for (auto& e : cleanCache) if (e.src == snap.source.get() && e.s == snap.clean) cleaned = e.buf;
                }
                if (cleaned == nullptr)
                {
                    auto b = std::make_shared<juce::AudioBuffer<float>> (Cleaner::process (*snap.source, snap.notes, snap.clean));
                    std::lock_guard<std::mutex> l (cleanMutex);
                    cleanCache.push_back ({ snap.source.get(), snap.clean, b });
                    if (cleanCache.size() > 8) cleanCache.erase (cleanCache.begin());
                    cleaned = b;
                }
            }
            auto r = renderTrack (snap, local, prof.get(), rate, cleaned != nullptr ? cleaned.get() : &snap.source->audio);
            r->signature = sig;
            callOnMessageThread (aliveFlag, [this, r, id, sig]
            {
                {
                    std::lock_guard<std::mutex> l (resultMutex);
                    if (scheduledSig[id] != sig) return;
                    results[id] = r;
                    status[id] = "Ready";
                }
                publishPlayback();
                sendChangeMessage();
            });
        });
    }
}

void Session::publishPlayback()
{
    auto set = std::make_shared<PlaybackSet>();
    set->rate = playRate.load();
    set->mode = doc.outputMode;
    set->mono = doc.mono;
    set->stereo = doc.stereo;
    set->hasTracks = ! doc.tracks.empty();

    bool anySolo = false;
    for (auto& t : doc.tracks) anySolo |= t.solo;

    for (auto& t : doc.tracks)
    {
        auto r = result (t.id);
        if (r == nullptr) continue;
        if (t.mute || (anySolo && ! t.solo)) continue;
        PlaybackSet::Item it;
        it.r = r;
        const float g = juce::Decibels::decibelsToGain (t.gainDb);
        const float p = juce::jlimit (-1.0f, 1.0f, t.pan);
        const float ang = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        it.gainL = g * std::cos (ang) * 1.41421356f;
        it.gainR = g * std::sin (ang) * 1.41421356f;
        it.startSample = (juce::int64) std::llround (r->startSec * r->rate);
        it.lead = t.isLead;
        set->items.push_back (it);
    }
    if (onPlayback) onPlayback (set);
}

void Session::timerCallback()
{
    const double b = hostBpm.load();
    if (b > 1.0 && std::abs (b - doc.bpm) > 0.01)
    {
        doc.bpm = b;
        changed();
    }
}

//==============================================================================
juce::Array<juce::File> Session::exportForDrag (bool stems)
{
    juce::Array<juce::File> files;
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("HARMIVOX-6").getChildFile ("Exports").getChildFile (sessionId);
    dir.createDirectory();
    const double rate = playRate.load();

    bool anySolo = false;
    for (auto& t : doc.tracks) anySolo |= t.solo;

    juce::AudioBuffer<float> mix;
    for (auto& t : doc.tracks)
    {
        if (t.mute || (anySolo && ! t.solo)) continue;
        auto r = result (t.id);
        if (r == nullptr) continue;
        const auto start = (int) std::llround (juce::jmax (0.0, r->startSec) * r->rate);
        const int len = start + r->stereo.getNumSamples();
        const float g = juce::Decibels::decibelsToGain (t.gainDb);
        const float ang = (juce::jlimit (-1.0f, 1.0f, t.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = g * std::cos (ang) * 1.41421356f, gr = g * std::sin (ang) * 1.41421356f;

        juce::AudioBuffer<float> b (2, len);
        b.clear();
        b.addFrom (0, start, r->stereo, 0, 0, r->stereo.getNumSamples(), gl);
        b.addFrom (1, start, r->stereo, 1, 0, r->stereo.getNumSamples(), gr);
        if (stems)
        {
            auto f = dir.getChildFile (juce::File::createLegalFileName ("HARMIVOX-6 - " + t.name) + ".wav");
            if (writeWav (f, b, rate, 24)) files.add (f);
        }
        else
        {
            if (mix.getNumSamples() < len) mix.setSize (2, len, true, true, false);
            mix.addFrom (0, 0, b, 0, 0, len);
            mix.addFrom (1, 0, b, 1, 0, len);
        }
    }
    if (! stems && mix.getNumSamples() > 0)
    {
        auto f = dir.getChildFile ("HARMIVOX-6 - Vocal stack.wav");
        if (writeWav (f, mix, rate, 24)) files.add (f);
    }
    return files;
}

//==============================================================================
namespace
{
void putNote (juce::XmlElement& e, const Note& n)
{
    e.setAttribute ("id", n.id); e.setAttribute ("s", n.srcStart); e.setAttribute ("e", n.srcEnd);
    e.setAttribute ("det", n.detected); e.setAttribute ("tgt", n.target); e.setAttribute ("vol", n.volumeDb);
    e.setAttribute ("form", n.formantSt); e.setAttribute ("drift", n.driftFix); e.setAttribute ("vib", n.vibrato);
    e.setAttribute ("addv", n.addVibratoCents); e.setAttribute ("rate", n.vibratoRateHz);
    e.setAttribute ("tr", n.transition); e.setAttribute ("mute", n.muted); e.setAttribute ("edited", n.edited);
}
Note getNote (const juce::XmlElement& e)
{
    Note n;
    n.id = e.getIntAttribute ("id"); n.srcStart = e.getDoubleAttribute ("s"); n.srcEnd = e.getDoubleAttribute ("e");
    n.detected = (float) e.getDoubleAttribute ("det"); n.target = (float) e.getDoubleAttribute ("tgt");
    n.volumeDb = (float) e.getDoubleAttribute ("vol"); n.formantSt = (float) e.getDoubleAttribute ("form");
    n.driftFix = (float) e.getDoubleAttribute ("drift", -1); n.vibrato = (float) e.getDoubleAttribute ("vib", -1);
    n.addVibratoCents = (float) e.getDoubleAttribute ("addv"); n.vibratoRateHz = (float) e.getDoubleAttribute ("rate", 5.5);
    n.transition = (float) e.getDoubleAttribute ("tr", -1); n.muted = e.getBoolAttribute ("mute");
    n.edited = e.getBoolAttribute ("edited");
    return n;
}
void putFinish (juce::XmlElement& e, const FinishSettings& f)
{
    e.setAttribute ("chain", f.chain); e.setAttribute ("on", f.enabled); e.setAttribute ("lowcut", f.lowCutHz);
    e.setAttribute ("deplosive", f.deplosive); e.setAttribute ("mud", f.mudCutDb); e.setAttribute ("deess", f.deEsser);
    e.setAttribute ("presence", f.presenceDb); e.setAttribute ("air", f.airDb); e.setAttribute ("comp", f.compressDb);
    e.setAttribute ("ratio", f.ratio);
}
FinishSettings getFinish (const juce::XmlElement* e)
{
    FinishSettings f;
    if (e == nullptr) return f;
    f.chain = e->getIntAttribute ("chain", f.chain); f.enabled = e->getBoolAttribute ("on", true);
    f.lowCutHz = (float) e->getDoubleAttribute ("lowcut"); f.deplosive = (float) e->getDoubleAttribute ("deplosive", f.deplosive);
    f.mudCutDb = (float) e->getDoubleAttribute ("mud", f.mudCutDb); f.deEsser = (float) e->getDoubleAttribute ("deess", f.deEsser);
    f.presenceDb = (float) e->getDoubleAttribute ("presence", f.presenceDb); f.airDb = (float) e->getDoubleAttribute ("air", f.airDb);
    f.compressDb = (float) e->getDoubleAttribute ("comp", f.compressDb); f.ratio = (float) e->getDoubleAttribute ("ratio", f.ratio);
    return f;
}
} // namespace

std::unique_ptr<juce::XmlElement> Session::toXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("HARMIVOX6");
    x->setAttribute ("version", 1);
    x->setAttribute ("session", sessionId);
    x->setAttribute ("profile", profileFileName);

    auto* d = x->createNewChildElement ("DOC");
    d->setAttribute ("autoKey", doc.autoKey); d->setAttribute ("key", doc.key); d->setAttribute ("scale", doc.scale);
    d->setAttribute ("autoChords", doc.autoChords); d->setAttribute ("bpm", doc.bpm); d->setAttribute ("bpb", doc.beatsPerBar);
    d->setAttribute ("out", (int) doc.outputMode); d->setAttribute ("mono", doc.mono); d->setAttribute ("finishAll", doc.finishAllTracks);
    d->setAttribute ("nextTrack", doc.nextTrackId); d->setAttribute ("nextNote", doc.nextNoteId);
    putFinish (*d->createNewChildElement ("FINISH"), doc.finish);
    auto* st = d->createNewChildElement ("STEREO");
    st->setAttribute ("imager", doc.stereo.imager); st->setAttribute ("low", doc.stereo.lowWidth);
    st->setAttribute ("high", doc.stereo.highWidth); st->setAttribute ("stz", doc.stereo.stereoize);
    st->setAttribute ("stzAmt", doc.stereo.stereoizeAmt);
    if (! doc.autoChords)
        for (auto& c : doc.chords)
        {
            auto* ce = d->createNewChildElement ("CHORD");
            ce->setAttribute ("s", c.start); ce->setAttribute ("e", c.end); ce->setAttribute ("root", c.root); ce->setAttribute ("q", c.quality);
        }

    for (auto& t : doc.tracks)
    {
        auto* te = d->createNewChildElement ("TRACK");
        te->setAttribute ("id", t.id); te->setAttribute ("name", t.name); te->setAttribute ("colour", t.colour.toString());
        te->setAttribute ("voice", t.isVoice()); te->setAttribute ("lead", t.isLead);
        te->setAttribute ("source", t.source != nullptr ? t.source->fileName : juce::String());
        te->setAttribute ("clip", t.clipStart); te->setAttribute ("gain", t.gainDb); te->setAttribute ("pan", t.pan);
        te->setAttribute ("mute", t.mute); te->setAttribute ("solo", t.solo); te->setAttribute ("profile", t.useProfile);
        auto* tu = te->createNewChildElement ("TUNE");
        tu->setAttribute ("preset", t.tune.preset); tu->setAttribute ("strength", t.tune.strength);
        tu->setAttribute ("retune", t.tune.retuneMs); tu->setAttribute ("drift", t.tune.driftFix); tu->setAttribute ("vib", t.tune.vibrato);
        auto* cl = te->createNewChildElement ("CLEAN");
        cl->setAttribute ("nr", t.clean.noiseReduce); cl->setAttribute ("breath", t.clean.breathDb);
        cl->setAttribute ("gate", t.clean.gate); cl->setAttribute ("click", t.clean.deClick);
        auto* hu = te->createNewChildElement ("HUMAN");
        hu->setAttribute ("seed", (int) t.human.seed); hu->setAttribute ("var", t.human.variation);
        hu->setAttribute ("other", t.human.otherSinger); hu->setAttribute ("belt", t.human.belt);
        hu->setAttribute ("loose", t.human.looseMs); hu->setAttribute ("tscale", t.human.timingScale);
        putFinish (*te->createNewChildElement ("FINISH"), t.finish);
        auto* vo = te->createNewChildElement ("VSPEC");
        vo->setAttribute ("parent", t.voice.parentId); vo->setAttribute ("steps", t.voice.steps);
        vo->setAttribute ("semis", t.voice.semitones); vo->setAttribute ("mode", (int) t.voice.mode);
        vo->setAttribute ("follow", t.voice.followParent);
        for (auto& n : t.notes) putNote (*te->createNewChildElement ("NOTE"), n);
        for (auto& w : t.warps) { auto* we = te->createNewChildElement ("WARP"); we->setAttribute ("s", w.src); we->setAttribute ("d", w.dst); }
    }
    return x;
}

void Session::fromXml (const juce::XmlElement& x)
{
    if (! x.hasTagName ("HARMIVOX6")) return;
    sessionId = x.getStringAttribute ("session", sessionId);
    profileFileName = x.getStringAttribute ("profile");
    if (profileFileName.isNotEmpty())
    {
        auto f = VoiceProfile::folder().getChildFile (profileFileName);
        if (f.existsAsFile()) voiceProfile = std::make_shared<const VoiceProfile> (VoiceProfile::load (f));
    }

    pendingXml = std::make_unique<juce::XmlElement> (x);
    restoredSources.clear();
    juce::StringArray files;
    if (auto* d = x.getChildByName ("DOC"))
        for (auto* te : d->getChildWithTagNameIterator ("TRACK"))
            files.addIfNotAlreadyThere (te->getStringAttribute ("source"));
    files.removeEmptyStrings();

    restoreWaiting = files.size();
    if (restoreWaiting == 0) { restoreFromPendingXml(); return; }

    pendingImportCount += files.size();
    for (auto& fn : files)
    {
        auto file = folder().getChildFile (fn);
        auto aliveFlag = alive;
        pool.addJob ([this, aliveFlag, file, fn]
        {
            std::shared_ptr<SourceAudio> src;
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (file) })
            {
                juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
                r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                src = Analysis::analyse (toMono (b), r->sampleRate, file.getFileNameWithoutExtension());
                if (src) src->fileName = fn;
            }
            callOnMessageThread (aliveFlag, [this, src, fn]
            {
                --pendingImportCount;
                if (src) restoredSources[fn] = src;
                if (--restoreWaiting == 0) restoreFromPendingXml();
            });
        });
    }
    sendChangeMessage();
}

void Session::restoreFromPendingXml()
{
    if (pendingXml == nullptr) return;
    auto* d = pendingXml->getChildByName ("DOC");
    if (d == nullptr) { pendingXml.reset(); return; }

    Document nd;
    nd.autoKey = d->getBoolAttribute ("autoKey", true); nd.key = d->getIntAttribute ("key"); nd.scale = d->getIntAttribute ("scale");
    nd.autoChords = d->getBoolAttribute ("autoChords", true); nd.bpm = d->getDoubleAttribute ("bpm", 90);
    nd.beatsPerBar = d->getIntAttribute ("bpb", 4); nd.outputMode = (OutputMode) d->getIntAttribute ("out");
    nd.mono = d->getBoolAttribute ("mono"); nd.finishAllTracks = d->getBoolAttribute ("finishAll", true);
    nd.nextTrackId = d->getIntAttribute ("nextTrack", 1); nd.nextNoteId = d->getIntAttribute ("nextNote", 1);
    nd.finish = getFinish (d->getChildByName ("FINISH"));
    if (auto* st = d->getChildByName ("STEREO"))
    {
        nd.stereo.imager = st->getBoolAttribute ("imager", true); nd.stereo.lowWidth = (float) st->getDoubleAttribute ("low", 0.55);
        nd.stereo.highWidth = (float) st->getDoubleAttribute ("high", 1.1); nd.stereo.stereoize = st->getBoolAttribute ("stz", true);
        nd.stereo.stereoizeAmt = (float) st->getDoubleAttribute ("stzAmt", 0.05);
    }
    for (auto* ce : d->getChildWithTagNameIterator ("CHORD"))
        nd.chords.push_back ({ ce->getDoubleAttribute ("s"), ce->getDoubleAttribute ("e"), ce->getIntAttribute ("root"), ce->getIntAttribute ("q") });

    for (auto* te : d->getChildWithTagNameIterator ("TRACK"))
    {
        Track t;
        t.id = te->getIntAttribute ("id"); t.name = te->getStringAttribute ("name");
        t.colour = juce::Colour::fromString (te->getStringAttribute ("colour", "ffff6a3d"));
        t.kind = te->getBoolAttribute ("voice") ? TrackKind::Voice : TrackKind::Take;
        t.isLead = te->getBoolAttribute ("lead");
        auto it = restoredSources.find (te->getStringAttribute ("source"));
        if (it == restoredSources.end()) continue;
        t.source = it->second;
        t.clipStart = te->getDoubleAttribute ("clip"); t.gainDb = (float) te->getDoubleAttribute ("gain");
        t.pan = (float) te->getDoubleAttribute ("pan"); t.mute = te->getBoolAttribute ("mute"); t.solo = te->getBoolAttribute ("solo");
        t.useProfile = te->getBoolAttribute ("profile", true);
        if (auto* tu = te->getChildByName ("TUNE"))
        {
            t.tune.preset = tu->getIntAttribute ("preset", 3); t.tune.strength = (float) tu->getDoubleAttribute ("strength", 1);
            t.tune.retuneMs = (float) tu->getDoubleAttribute ("retune", 40); t.tune.driftFix = (float) tu->getDoubleAttribute ("drift");
            t.tune.vibrato = (float) tu->getDoubleAttribute ("vib", 1);
        }
        if (auto* cl = te->getChildByName ("CLEAN"))
        {
            t.clean.noiseReduce = (float) cl->getDoubleAttribute ("nr"); t.clean.breathDb = (float) cl->getDoubleAttribute ("breath");
            t.clean.gate = (float) cl->getDoubleAttribute ("gate"); t.clean.deClick = (float) cl->getDoubleAttribute ("click");
        }
        if (auto* hu = te->getChildByName ("HUMAN"))
        {
            t.human.seed = (juce::uint32) hu->getIntAttribute ("seed", 1); t.human.variation = (float) hu->getDoubleAttribute ("var");
            t.human.otherSinger = (float) hu->getDoubleAttribute ("other"); t.human.belt = (float) hu->getDoubleAttribute ("belt");
            t.human.looseMs = (float) hu->getDoubleAttribute ("loose", 12); t.human.timingScale = (float) hu->getDoubleAttribute ("tscale", 1);
        }
        t.finish = getFinish (te->getChildByName ("FINISH"));
        if (auto* vo = te->getChildByName ("VSPEC"))
        {
            t.voice.parentId = vo->getIntAttribute ("parent", -1); t.voice.steps = vo->getIntAttribute ("steps");
            t.voice.semitones = vo->getIntAttribute ("semis"); t.voice.mode = (IntervalMode) vo->getIntAttribute ("mode");
            t.voice.followParent = vo->getBoolAttribute ("follow", true);
        }
        for (auto* ne : te->getChildWithTagNameIterator ("NOTE")) t.notes.push_back (getNote (*ne));
        for (auto* we : te->getChildWithTagNameIterator ("WARP")) t.warps.push_back ({ we->getDoubleAttribute ("s"), we->getDoubleAttribute ("d") });
        nd.tracks.push_back (std::move (t));
    }
    pendingXml.reset();
    restoredSources.clear();
    doc = std::move (nd);
    undoStack.clear(); redoStack.clear();
    changed();
}

} // namespace hvx
