#pragma once
// HARMIVOX-6 — core data model.
// A Document holds every track (takes and generated voices), the musical
// context (key, scale, chords) and the shared settings. It is a plain value
// type: copying it is cheap because audio is shared through shared_ptrs, which
// is what makes undo snapshots and background rendering simple.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <vector>
#include <array>

namespace hvx
{
//==============================================================================
struct PitchFrame
{
    float midi    = 0.0f;   // detected pitch (MIDI note number, fractional)
    float clarity = 0.0f;   // 0..1, how periodic the frame is
    float rmsDb   = -120.0f;
    bool  voiced  = false;
};

struct PitchMark
{
    int   pos     = 0;      // sample position of the glottal pulse / grain centre
    float period  = 220.0f; // samples
    bool  voiced  = false;
};

/** Immutable analysed audio. Takes own one; voices share their parent's. */
struct SourceAudio
{
    juce::AudioBuffer<float> audio;     // mono
    double sampleRate = 44100.0;
    int hop = 256;                      // analysis hop in samples
    std::vector<PitchFrame> frames;
    std::vector<PitchMark>  marks;
    juce::String name;
    juce::String fileName;              // file inside the session folder

    int    numSamples() const      { return audio.getNumSamples(); }
    double lengthSeconds() const   { return numSamples() / sampleRate; }
    double frameToSeconds (double f) const { return f * hop / sampleRate; }
    double secondsToFrame (double s) const { return s * sampleRate / hop; }

    /** Interpolated pitch at a source time; returns false if unvoiced. */
    bool pitchAt (double seconds, float& midiOut) const;
    float rmsDbAt (double seconds) const;
};
using SourcePtr = std::shared_ptr<const SourceAudio>;

//==============================================================================
struct Note
{
    int    id = 0;
    double srcStart = 0, srcEnd = 0;  // seconds in the source audio
    float  detected = 60.0f;          // what was sung (median pitch)
    float  target   = 60.0f;          // where it should land (fractional = cents)

    float volumeDb  = 0.0f;
    float formantSt = 0.0f;
    float driftFix  = -1.0f;          // -1 = follow track setting
    float vibrato   = -1.0f;          // -1 = follow track setting
    float addVibratoCents = 0.0f;
    float vibratoRateHz   = 5.5f;
    float transition = -1.0f;         // join INTO this note, 0..1; -1 = auto
    bool  muted = false;
    bool  edited = false;             // pitch moved by hand (key changes leave it alone)
};

struct WarpPoint
{
    double src = 0;   // seconds in source
    double dst = 0;   // seconds on the track's output timeline
};

struct TuneSettings
{
    int   preset    = 3;       // 0 Natural, 1 Pop, 2 Hard tune, 3 Custom
    float strength  = 1.0f;    // 0..1
    float retuneMs  = 40.0f;   // 0..400
    float driftFix  = 0.0f;    // 0..1
    float vibrato   = 1.0f;    // 0..1.5 (keep / scale natural vibrato)
};

struct CleanSettings
{
    float noiseReduce = 0.0f;  // 0..1
    float breathDb    = 0.0f;  // 0..24 dB reduction
    float gate        = 0.0f;  // 0..1
    float deClick     = 0.0f;  // 0..1
    bool  operator== (const CleanSettings& o) const
    { return noiseReduce == o.noiseReduce && breathDb == o.breathDb && gate == o.gate && deClick == o.deClick; }
};

struct HumanizeSettings
{
    juce::uint32 seed = 1;
    float variation   = 0.0f;  // 0..1  extra randomness for this voice
    float otherSinger = 0.0f;  // 0..1  formant/tone change ("someone else")
    float belt        = 0.0f;  // 0..1  pushed, brighter delivery
    float looseMs     = 12.0f; // timing spread
    float timingScale = 1.0f;  // 0..1 shrinks looseness (Align to lead)
};

struct FinishSettings
{
    int   chain      = 2;      // 0 Off, 1 Natural, 2 Pop, 3 Rap, 4 Airy
    bool  enabled    = true;
    float lowCutHz   = 0.0f;   // 0 = auto
    float deplosive  = 0.6f;   // 0..1
    float mudCutDb   = -2.5f;  // -12..0
    float deEsser    = 0.7f;   // 0..1
    float presenceDb = 2.0f;   // -6..9
    float airDb      = 3.0f;   // -6..9
    float compressDb = 6.0f;   // 0..18 (gain reduction on peaks)
    float ratio      = 4.2f;   // 1..10
};

struct StereoSettings
{
    bool  imager    = true;
    float lowWidth  = 0.55f;   // 0..2 (below ~300 Hz)
    float highWidth = 1.10f;   // 0..2
    bool  stereoize = true;
    float stereoizeAmt = 0.05f;// 0..1
};

enum class TrackKind { Take, Voice };

enum class IntervalMode { FollowKey = 0, FollowChords = 1, Exact = 2 };

struct VoiceSpec
{
    int parentId   = -1;
    int steps      = 0;        // scale steps: 0 double, 2 = 3rd, 4 = 5th, 7 = octave; sign = above/below
    int semitones  = 0;        // used in Exact mode
    IntervalMode mode = IntervalMode::FollowKey;
    bool followParent = true;  // regenerate notes when the parent changes
};

struct Track
{
    int id = 0;
    juce::String name;
    juce::Colour colour;
    TrackKind kind = TrackKind::Take;
    bool isLead = false;

    SourcePtr source;                  // takes: own audio. voices: parent's audio
    double clipStart = 0.0;            // host-timeline seconds where output time 0 sits

    std::vector<Note> notes;
    std::vector<WarpPoint> warps;      // empty = no time changes

    float gainDb = 0.0f;
    float pan    = 0.0f;               // -1..1
    bool  mute = false, solo = false;

    TuneSettings     tune;
    CleanSettings    clean;
    HumanizeSettings human;
    FinishSettings   finish;
    VoiceSpec        voice;
    bool useProfile = true;            // shape with the Voice lab profile

    bool isVoice() const { return kind == TrackKind::Voice; }
};

struct Chord
{
    double start = 0, end = 0;   // seconds, host timeline
    int root = 0;                // pitch class, -1 = no chord
    int quality = 0;             // see chordQualityNames()
};

enum class OutputMode { Replace = 0, Blend = 1, InputOnly = 2 };

struct Document
{
    std::vector<Track> tracks;
    int nextTrackId = 1;
    int nextNoteId  = 1;

    bool autoKey = true;
    int  key   = 0;              // 0 = C
    int  scale = 0;              // see scaleNames()

    bool autoChords = true;
    std::vector<Chord> chords;

    FinishSettings finish;       // used when finishAllTracks
    bool finishAllTracks = true;
    StereoSettings stereo;

    double bpm = 90.0;
    int    beatsPerBar = 4;

    OutputMode outputMode = OutputMode::Replace;
    bool mono = false;

    Track*       find (int id);
    const Track* find (int id) const;
    Track*       lead();
    const Track* lead() const;
    int          indexOf (int id) const;
};

//==============================================================================
// Musical helpers
const juce::StringArray& scaleNames();
const std::vector<int>&  scaleIntervals (int scale);
const juce::StringArray& keyNames();
const juce::StringArray& chordQualityNames();
std::vector<int> chordPitchClasses (int root, int quality);
juce::String noteName (float midi);   // "C4" = MIDI 60
juce::String chordName (const Chord&);

bool inScale (int midi, int key, int scale);
int  snapToScale (float midi, int key, int scale);  // nearest scale note

/** Warp maps (piecewise linear, monotonic). */
double srcToDst (const std::vector<WarpPoint>& w, double src);
double dstToSrc (const std::vector<WarpPoint>& w, double dst);

juce::Colour trackColourForIndex (int index);

} // namespace hvx
