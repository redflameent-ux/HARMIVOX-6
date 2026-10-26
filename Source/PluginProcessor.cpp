#include "PluginProcessor.h"
#include "ui/MainEditor.h"

HarmivoxProcessor::HarmivoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    session.onPlayback = [this] (std::shared_ptr<hvx::PlaybackSet> s)
    {
        {
            const juce::SpinLock::ScopedLockType l (playLock);
            std::swap (playSet, s);
        }
        const juce::SpinLock::ScopedLockType l (retiredLock);
        if (s != nullptr) retired.push_back (std::move (s));   // freed later on the message thread
    };
    startTimerHz (30);
}

HarmivoxProcessor::~HarmivoxProcessor()
{
    aliveFlag->store (false);
    stopTimer();
    session.onPlayback = nullptr;
}

bool HarmivoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in.isDisabled();
}

void HarmivoxProcessor::prepareToPlay (double sr, int block)
{
    sampleRateNow.store (sr);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, block), 2 };
    lowF.prepare (spec);  lowF.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);  lowF.setCutoffFrequency (300.0f);
    highF.prepare (spec); highF.setType (juce::dsp::LinkwitzRileyFilterType::highpass); highF.setCutoffFrequency (300.0f);
    mixBuf.setSize (2, juce::jmax (1, block) * 2, false, false, true);
    lowBuf.setSize (2, juce::jmax (1, block) * 2, false, false, true);
    session.setPlaybackRate (sr);
}

void HarmivoxProcessor::setInternalPlaying (bool p)
{
    if (p) internalPos.store (position.load());
    internalPlaying.store (p);
}

//==============================================================================
void HarmivoxProcessor::armHostCapture()
{
    fifo.reset();
    captured.clear();
    capturedSamples.store (0);
    captureEnded.store (false);
    expectedHostTime = -1.0;
    captureMode.store ((int) Capture::WaitingForHost);
}

void HarmivoxProcessor::cancelCapture()
{
    captureMode.store ((int) Capture::Off);
    fifo.reset();
    captured.clear();
    capturedSamples.store (0);
}

void HarmivoxProcessor::startFreeCapture()
{
    fifo.reset();
    captured.clear();
    capturedSamples.store (0);
    captureEnded.store (false);
    captureMode.store ((int) Capture::Free);
}

juce::AudioBuffer<float> HarmivoxProcessor::stopFreeCapture()
{
    captureMode.store ((int) Capture::Off);
    timerCallback();   // drain what is left
    juce::AudioBuffer<float> b (1, (int) captured.size());
    if (! captured.empty()) b.copyFrom (0, 0, captured.data(), (int) captured.size());
    captured.clear();
    return b;
}

int HarmivoxProcessor::copyRecentInput (float* dest, int num) const
{
    const int n = (int) recent.size();
    num = juce::jmin (num, n);
    const int end = recentPos.load();
    for (int i = 0; i < num; ++i)
        dest[i] = recent[(size_t) ((end - num + i + n) % n)];
    return num;
}

void HarmivoxProcessor::timerCallback()
{
    // drain capture fifo
    int ready = fifo.getNumReady();
    while (ready > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        captured.insert (captured.end(), fifoData.begin() + s1, fifoData.begin() + s1 + n1);
        if (n2 > 0) captured.insert (captured.end(), fifoData.begin() + s2, fifoData.begin() + s2 + n2);
        fifo.finishedRead (n1 + n2);
        ready = fifo.getNumReady();
    }

    if (captureEnded.exchange (false)) finishCapture (true);

    {
        const juce::SpinLock::ScopedLockType l (retiredLock);
        retired.clear();
    }
    session.setHostBpm (hostBpm.load());
}

void HarmivoxProcessor::finishCapture (bool keep)
{
    if (keep && captured.size() > (size_t) (0.5 * sampleRateNow.load()))
    {
        juce::AudioBuffer<float> b (1, (int) captured.size());
        b.copyFrom (0, 0, captured.data(), (int) captured.size());
        int takes = 0;
        for (auto& t : session.doc.tracks) if (! t.isVoice()) ++takes;
        session.importAudio (b, sampleRateNow.load(), "Take " + juce::String (takes + 1), captureStart.load());
    }
    captured.clear();
    capturedSamples.store (0);
}

//==============================================================================
void HarmivoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const double sr = getSampleRate() > 0 ? getSampleRate() : sampleRateNow.load();

    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);

    // host transport ---------------------------------------------------------
    bool playing = false;
    double hostTime = position.load();
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            sawHost.store (true);
            playing = pos->getIsPlaying() || pos->getIsRecording();
            if (auto b = pos->getBpm()) hostBpm.store (*b);
            if (auto ts = pos->getTimeInSeconds()) hostTime = *ts;
            else if (auto smp = pos->getTimeInSamples()) hostTime = (double) *smp / sr;
            else if (auto ppq = pos->getPpqPosition()) hostTime = *ppq * 60.0 / juce::jmax (1.0, hostBpm.load());
        }
    }
    hostPlaying.store (playing && followHost.load());

    // input: meter, recent buffer, capture --------------------------------------
    float peak = 0.0f;
    {
        const int rn = (int) recent.size();
        int rp = recentPos.load();
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < juce::jmin (numIn, 2); ++c) v += buffer.getSample (c, i);
            if (numIn > 1) v *= 0.5f;
            peak = juce::jmax (peak, std::abs (v));
            recent[(size_t) rp] = v;
            rp = (rp + 1) % rn;
        }
        recentPos.store (rp);
        inLevel.store (juce::jmax (peak, inLevel.load() * 0.85f));
    }

    auto mode = (Capture) captureMode.load();
    if (mode == Capture::WaitingForHost && playing)
    {
        captureStart.store (hostTime);
        expectedHostTime = hostTime;
        captureMode.store ((int) Capture::RecordingHost);
        mode = Capture::RecordingHost;
    }
    if (mode == Capture::RecordingHost)
    {
        const bool jumped = expectedHostTime >= 0 && std::abs (hostTime - expectedHostTime) > 0.08;
        if (! playing || jumped)
        {
            captureMode.store ((int) Capture::Off);
            captureEnded.store (true);
            mode = Capture::Off;
        }
        expectedHostTime = hostTime + n / sr;
    }
    if (mode == Capture::RecordingHost || mode == Capture::Free)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto mono = [&] (int i)
        {
            float v = 0.0f;
            for (int c = 0; c < juce::jmin (numIn, 2); ++c) v += buffer.getSample (c, i);
            return numIn > 1 ? v * 0.5f : v;
        };
        for (int i = 0; i < n1; ++i) fifoData[(size_t) (s1 + i)] = mono (i);
        for (int i = 0; i < n2; ++i) fifoData[(size_t) (s2 + i)] = mono (n1 + i);
        fifo.finishedWrite (n1 + n2);
        capturedSamples.fetch_add (n1 + n2);
    }

    // position ----------------------------------------------------------------
    double pos;
    if (hostPlaying.load()) { pos = hostTime; internalPos.store (hostTime); }
    else if (internalPlaying.load()) { pos = internalPos.load(); internalPos.store (pos + n / sr); }
    else pos = sawHost.load() && followHost.load() ? hostTime : internalPos.load();
    position.store (pos);
    labClock.store (labClock.load() + n / sr);

    const bool audible = hostPlaying.load() || internalPlaying.load();

    // mix the rendered tracks ----------------------------------------------------
    if (mixBuf.getNumSamples() < n) { mixBuf.setSize (2, n, false, false, true); lowBuf.setSize (2, n, false, false, true); }
    mixBuf.clear (0, 0, n); mixBuf.clear (1, 0, n);

    hvx::OutputMode outMode = hvx::OutputMode::Replace;
    bool hasTracks = false, monoOut = false, mixed = false;
    hvx::StereoSettings stereo;
    {
        const juce::SpinLock::ScopedTryLockType l (playLock);
        if (l.isLocked() && playSet != nullptr)
        {
            outMode = playSet->mode;
            hasTracks = playSet->hasTracks;
            monoOut = playSet->mono;
            stereo = playSet->stereo;
            if (audible && std::abs (playSet->rate - sr) < 1.0)
            {
                const auto posSample = (juce::int64) std::llround (pos * sr);
                for (auto& it : playSet->items)
                {
                    const auto& b = it.r->stereo;
                    const juce::int64 off = posSample - it.startSample;
                    const juce::int64 len = b.getNumSamples();
                    if (off + n <= 0 || off >= len) continue;
                    const int dstStart = (int) juce::jmax ((juce::int64) 0, -off);
                    const int srcStart = (int) juce::jmax ((juce::int64) 0, off);
                    const int num = (int) juce::jmin ((juce::int64) (n - dstStart), len - srcStart);
                    if (num <= 0) continue;
                    mixBuf.addFrom (0, dstStart, b, 0, srcStart, num, it.gainL);
                    mixBuf.addFrom (1, dstStart, b, 1, srcStart, num, it.gainR);
                    mixed = true;
                }
            }
        }
    }

    if (mixed && stereo.imager && (std::abs (stereo.lowWidth - 1.0f) > 0.01f || std::abs (stereo.highWidth - 1.0f) > 0.01f))
    {
        float* L = mixBuf.getWritePointer (0);
        float* R = mixBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float lL = lowF.processSample (0, L[i]), lR = lowF.processSample (1, R[i]);
            const float hL = highF.processSample (0, L[i]), hR = highF.processSample (1, R[i]);
            const float lm = 0.5f * (lL + lR), ls = 0.5f * (lL - lR) * stereo.lowWidth;
            const float hm = 0.5f * (hL + hR), hs = 0.5f * (hL - hR) * stereo.highWidth;
            L[i] = lm + ls + hm + hs;
            R[i] = lm - ls + hm - hs;
        }
    }

    const bool capturing = mode == Capture::RecordingHost || mode == Capture::Free || mode == Capture::WaitingForHost;
    const bool passInput = outMode == hvx::OutputMode::InputOnly || capturing || ! hasTracks
                           || outMode == hvx::OutputMode::Blend;

    if (numIn == 1 && numOut >= 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    if (! passInput) for (int c = 0; c < numOut; ++c) buffer.clear (c, 0, n);

    if (outMode != hvx::OutputMode::InputOnly && ! capturing && mixed)
    {
        buffer.addFrom (0, 0, mixBuf, 0, 0, n);
        if (numOut > 1) buffer.addFrom (1, 0, mixBuf, 1, 0, n);
        else buffer.addFrom (0, 0, mixBuf, 1, 0, n);
    }

    // voice lab guide note + click ------------------------------------------------
    const float gm = guideMidi.load();
    const float cb = clickBpm.load();
    if (gm > 0.0f || cb > 0.0f)
    {
        const double f = gm > 0 ? 440.0 * std::pow (2.0, (gm - 69.0) / 12.0) : 0.0;
        const float gl = guideLevel.load() * 0.25f, cl = clickLevel.load() * 0.5f;
        const double beat = cb > 0 ? 60.0 / cb : 0.0;
        const double t0 = labClock.load() - n / sr;
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            if (gm > 0.0f)
            {
                guidePhase += f / sr;
                if (guidePhase > 1.0) guidePhase -= 1.0;
                const double ph = guidePhase * juce::MathConstants<double>::twoPi;
                v += gl * (float) (std::sin (ph) * 0.8 + std::sin (2 * ph) * 0.15 + std::sin (3 * ph) * 0.05);
            }
            if (cb > 0.0f)
            {
                const double t = t0 + i / sr - clickStartSec.load();
                if (t >= 0)
                {
                    const auto idx = (juce::int64) std::floor (t / beat);
                    if (idx != lastClickIndex) { lastClickIndex = idx; clickPhaseEnv = (idx % 4 == 0) ? 1.0 : 0.6; }
                }
                if (clickPhaseEnv > 0.001)
                {
                    v += cl * (float) (clickPhaseEnv * std::sin (juce::MathConstants<double>::twoPi * 1500.0 * (t0 + i / sr)));
                    clickPhaseEnv *= std::exp (-1.0 / (0.012 * sr));
                }
            }
            for (int c = 0; c < numOut; ++c) buffer.addSample (c, i, v);
        }
    }
    else lastClickIndex = -1;

    if (monoOut && numOut > 1)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]); L[i] = R[i] = m; }
    }
}

//==============================================================================
juce::AudioProcessorEditor* HarmivoxProcessor::createEditor()
{
    return new HarmivoxEditor (*this);
}

void HarmivoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto x = session.toXml();
    x->setAttribute ("page", lastPage);
    x->setAttribute ("w", editorW);
    x->setAttribute ("h", editorH);
    x->setAttribute ("followHost", followHost.load());
    copyXmlToBinary (*x, destData);
}

void HarmivoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto x = getXmlFromBinary (data, sizeInBytes))
    {
        lastPage = x->getIntAttribute ("page", 0);
        editorW = x->getIntAttribute ("w", 1280);
        editorH = x->getIntAttribute ("h", 820);
        followHost.store (x->getBoolAttribute ("followHost", true));
        if (juce::MessageManager::existsAndIsCurrentThread())
            session.fromXml (*x);
        else
        {
            auto copy = std::make_shared<juce::XmlElement> (*x);
            auto alive = aliveFlag;
            juce::MessageManager::callAsync ([this, copy, alive] { if (alive->load()) session.fromXml (*copy); });
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmivoxProcessor();
}
