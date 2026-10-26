#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "engine/Session.h"

class HarmivoxProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    HarmivoxProcessor();
    ~HarmivoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HARMIVOX-6"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    hvx::Session session;

    // Transport --------------------------------------------------------------
    double positionSeconds() const     { return position.load(); }
    bool   isHostPlaying() const       { return hostPlaying.load(); }
    bool   isInternalPlaying() const   { return internalPlaying.load(); }
    bool   isPlaying() const           { return hostPlaying.load() || internalPlaying.load(); }
    void   setInternalPlaying (bool p);
    void   setInternalPosition (double sec) { internalPos.store (sec); if (! hostPlaying.load()) position.store (sec); }
    double hostBpmValue() const        { return hostBpm.load(); }
    bool   hostKnown() const           { return sawHost.load(); }
    std::atomic<bool> followHost { true };

    // Capture from the insert ------------------------------------------------
    enum class Capture { Off = 0, WaitingForHost, RecordingHost, Free };
    void armHostCapture();
    void cancelCapture();
    void stopHostCapture() { if (captureMode.load() == (int) Capture::RecordingHost) { captureMode.store ((int) Capture::Off); captureEnded.store (true); } else cancelCapture(); }
    Capture captureState() const       { return (Capture) captureMode.load(); }
    double captureSeconds() const      { return capturedSamples.load() / juce::jmax (1.0, sampleRateNow.load()); }

    /** Free capture (Voice lab): records until stopFreeCapture. */
    void startFreeCapture();
    juce::AudioBuffer<float> stopFreeCapture();

    float inputLevel() const           { return inLevel.load(); }
    /** Copies the most recent input (mono) for live pitch display. */
    int   copyRecentInput (float* dest, int num) const;
    double currentSampleRate() const   { return sampleRateNow.load(); }

    // Voice lab guide ---------------------------------------------------------
    std::atomic<float> guideMidi { 0.0f };    // 0 = off
    std::atomic<float> guideLevel { 0.35f };
    std::atomic<float> clickBpm { 0.0f };     // 0 = off
    std::atomic<float> clickLevel { 0.5f };
    std::atomic<double> clickStartSec { 0.0 };
    double labClockSec() const { return labClock.load(); }

    // UI state kept with the project
    int lastPage = 0;
    int editorW = 1280, editorH = 820;

private:
    void timerCallback() override;
    void finishCapture (bool keep);

    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);
    juce::SpinLock playLock;
    std::shared_ptr<hvx::PlaybackSet> playSet;
    std::vector<std::shared_ptr<hvx::PlaybackSet>> retired;
    juce::SpinLock retiredLock;

    std::atomic<double> position { 0.0 }, internalPos { 0.0 }, hostBpm { 0.0 }, sampleRateNow { 44100.0 };
    std::atomic<bool> hostPlaying { false }, internalPlaying { false }, sawHost { false };
    std::atomic<double> labClock { 0.0 };

    // capture ring
    std::atomic<int> captureMode { 0 };
    std::atomic<double> captureStart { 0.0 };
    std::atomic<juce::int64> capturedSamples { 0 };
    std::atomic<bool> captureEnded { false };
    double expectedHostTime = -1.0;
    juce::AbstractFifo fifo { 1 << 19 };
    std::vector<float> fifoData = std::vector<float> (1 << 19);
    std::vector<float> captured;      // message thread only
    double capturedStartSec = 0.0;
    int takeCounter = 0;

    // recent input for meters
    std::vector<float> recent = std::vector<float> (8192, 0.0f);
    std::atomic<int> recentPos { 0 };
    std::atomic<float> inLevel { 0.0f };

    // mix bus imager
    juce::dsp::LinkwitzRileyFilter<float> lowF, highF;
    juce::AudioBuffer<float> mixBuf, lowBuf;
    double guidePhase = 0.0;
    double clickPhaseEnv = 0.0;
    juce::int64 lastClickIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmivoxProcessor)
};
