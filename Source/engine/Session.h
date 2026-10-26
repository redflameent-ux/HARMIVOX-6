#pragma once
#include "Model.h"
#include "Renderer.h"
#include "Harmony.h"
#include "VoiceProfile.h"
#include <juce_events/juce_events.h>
#include <map>
#include <mutex>
#include <atomic>

namespace hvx
{
struct RenderResult
{
    int trackId = 0;
    juce::AudioBuffer<float> stereo;     // at the playback rate
    double rate = 44100.0;
    double startSec = 0.0;               // host timeline
    juce::uint64 signature = 0;
};
using ResultPtr = std::shared_ptr<const RenderResult>;

/** Everything the audio thread needs to play the tracks. Immutable once published. */
struct PlaybackSet
{
    struct Item { ResultPtr r; float gainL = 1, gainR = 1; juce::int64 startSample = 0; bool lead = false; };
    std::vector<Item> items;
    double rate = 44100.0;
    OutputMode mode = OutputMode::Replace;
    bool mono = false;
    StereoSettings stereo;
    bool hasTracks = false;
};

/** Settings for a batch of new voices from the Add voices dialog. */
struct AddVoicesOptions
{
    int parentId = -1;
    std::vector<VoiceRequest> voices;
    IntervalMode mode = IntervalMode::FollowKey;
    float looseness   = 0.25f;   // 0..1
    float otherSingers = 0.0f;   // 0..1
    float panSpread   = 1.0f;    // 0..1
    float levelDb     = 0.0f;    // added to every voice
    bool  useProfile  = true;
};

class Session : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Session();
    ~Session() override;

    //==========================================================================
    Document doc;            // edit on the message thread, then call changed()

    /** Call BEFORE an edit you want to be undoable. */
    void checkpoint (const juce::String& label);
    /** Call AFTER editing doc: regenerates voices, schedules renders, updates playback. */
    void changed();

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();
    juce::StringArray historyLabels() const;   // oldest first, current state last
    int  historyPosition() const { return (int) undoStack.size(); }
    void jumpToHistory (int index);

    //==========================================================================
    void importAudio (const juce::AudioBuffer<float>& audio, double sampleRate,
                      const juce::String& name, double clipStartSec);
    bool importFile (const juce::File& file, double clipStartSec);
    int  pendingImports() const { return pendingImportCount.load(); }

    std::vector<int> addVoices (const AddVoicesOptions& o);
    void regenerateVoice (Track& voice) const;
    void reroll (int trackId);
    void resyncVoice (int trackId);
    void deleteTrack (int trackId);
    int  duplicateTrack (int trackId);
    void setLead (int trackId);

    void setKey (bool autoKey, int key, int scale);
    void retargetToKey (Track& t) const;

    void quantize (int trackId, int division, float amount);          // trackId -1 = all
    void alignToLead (int trackId, float tightness, bool alignEnds);  // trackId -1 = all
    void clearWarps (int trackId);                                    // -1 = all

    //==========================================================================
    void setPlaybackRate (double sr);
    double playbackRate() const { return playRate.load(); }
    void setHostBpm (double bpm) { hostBpm.store (bpm); }

    ResultPtr result (int trackId) const;
    std::shared_ptr<const Plan> planFor (int trackId);
    juce::String statusOf (int trackId) const;
    bool isBusy (int trackId) const;
    int  busyCount() const;
    float progressOf (int trackId) const;

    //==========================================================================
    std::shared_ptr<const VoiceProfile> profile() const { return voiceProfile; }
    void setProfile (std::shared_ptr<const VoiceProfile> p);
    juce::String profileFileName;    // "" = built-in shaping

    //==========================================================================
    /** Writes each audible track (or one mix) as WAV, padded to start at bar 1. */
    juce::Array<juce::File> exportForDrag (bool stemsNotMix);

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);
    juce::File folder() const;
    juce::String sessionId;

    std::function<void (std::shared_ptr<PlaybackSet>)> onPlayback;

    /** Pure render of one track (used by the worker and by the offline tests). */
    static std::shared_ptr<RenderResult> renderTrack (const Track& t, const Document& d,
                                                      const VoiceProfile* profile, double rate,
                                                      const juce::AudioBuffer<float>* cleanedAudio = nullptr);
    static juce::uint64 renderSignature (const Track& t, const Document& d, const juce::String& profileKey, double rate);
    static FinishSettings resolvedFinish (const Track& t, const Document& d);
    static CleanSettings  resolvedClean (const Track& t, const Document& d);

private:
    void timerCallback() override;
    void scheduleRenders();
    void publishPlayback();
    void finishImport (std::shared_ptr<SourceAudio> src, const juce::String& name, double clipStart, int sourceTrackId);
    void restoreFromPendingXml();
    std::shared_ptr<const juce::AudioBuffer<float>> cleanedFor (const Track& t, const Document& d);

    std::vector<std::pair<juce::String, Document>> undoStack, redoStack;
    juce::String lastLabel = "Start";

    juce::ThreadPool pool { 2 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::map<int, juce::uint64> scheduledSig;
    std::map<int, ResultPtr> results;
    std::map<int, juce::String> status;
    mutable std::mutex resultMutex;

    std::map<int, std::pair<juce::uint64, std::shared_ptr<const Plan>>> planCache;

    struct CleanCacheEntry { const SourceAudio* src; CleanSettings s; std::shared_ptr<const juce::AudioBuffer<float>> buf; };
    std::vector<CleanCacheEntry> cleanCache;
    std::mutex cleanMutex;

    std::atomic<double> playRate { 44100.0 };
    std::atomic<double> hostBpm  { 0.0 };
    std::atomic<int> pendingImportCount { 0 };
    std::shared_ptr<const VoiceProfile> voiceProfile;

    std::unique_ptr<juce::XmlElement> pendingXml;
    std::map<juce::String, SourcePtr> restoredSources;
    int restoreWaiting = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
} // namespace hvx
